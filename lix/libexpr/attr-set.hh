#pragma once
///@file

#include "lix/libexpr/nixexpr.hh"
#include "lix/libexpr/symbol-table.hh"

#include <boost/container/static_vector.hpp>

#include <algorithm>
#include <optional>
#include <span>

namespace nix {


class EvalMemory;
struct Value;

/**
 * Map one attribute name to its value.
 */
struct Attr
{
    /* the placement of `name` and `pos` in this struct is important.
       both of them are uint32 wrappers, they are next to each other
       to make sure that Attr has no padding on 64 bit machines. that
       way we keep Attr size at two words with no wasted space. */
    Symbol name;
    PosIdx pos;
    mutable Value value;
    Attr(Symbol name, Value value, PosIdx pos = noPos) : name(name), pos(pos), value(value) {}
    [[deprecated]]
    Attr() {};
    bool operator < (const Attr & a) const
    {
        return name < a.name;
    }
};

static_assert(sizeof(Attr) == 2 * sizeof(uint32_t) + sizeof(Value *),
    "performance of the evaluator is highly sensitive to the size of Attr. "
    "avoid introducing any padding into Attr if at all possible, and do not "
    "introduce new fields that need not be present for almost every instance.");

/**
 * Bindings contains all the attributes of an attribute set. It is defined
 * by its size and its capacity, the capacity being the number of Attr
 * elements allocated after this structure, while the size corresponds to
 * the number of elements already inserted in this structure.
 *
 * Bindings can be `//`-composed into an intrusive linked list of "layers" so
 * that a small right-hand side doesn't force a copy of a large left-hand side.
 * Lookups walk the chain top-down (upper layers shadow lower ones); iteration
 * does an on-the-fly k-way merge. Sharing an Attr between layered sets is
 * sound because the only in-place write to `Attr::value` is forceValue
 * replacing a thunk with that same thunk's result.
 *
 * Ported from CppNix (NixOS/nix#13987 plus the finishSizeIfNecessary speedup
 * from NixOS/nix@ec2fd2dc2).
 */
class alignas(Value::TAG_ALIGN) Bindings
{
public:
    using Size = uint32_t;
    PosIdx pos;

    static Bindings EMPTY;

private:
    /**
     * Number of attributes stored in this layer's own `attrs` array.
     */
    Size numAttrs = 0;

    /**
     * Number of attributes with unique names across the whole layer chain.
     * This is the user-visible size; `numAttrs` is an implementation detail.
     */
    Size numAttrsInChain = 0;

    uint32_t numLayers = 1;

    const Bindings * baseLayer = nullptr;

    Attr attrs[0];

    Bindings() = default;
    Bindings(const Bindings & bindings) = delete;

    friend class BindingsBuilder;

    /**
     * Bounds both lookup cost (a chain walk) and the iterator's fixed-size heap.
     */
    static constexpr unsigned MAX_LAYERS = 8;

public:
    Size size() const
    {
        return numAttrsInChain;
    }

    bool empty() const
    {
        return size() == 0;
    }

    class iterator
    {
    public:
        using value_type = Attr;
        using pointer = const Attr *;
        using reference = const Attr &;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::forward_iterator_tag;

    private:
        friend class Bindings;

        struct Cursor
        {
            pointer current;
            pointer end;
            /** Lower values shadow higher ones (0 is the topmost layer). */
            uint32_t priority;

            bool empty() const noexcept
            {
                return current == end;
            }

            void skipThrough(Symbol name) noexcept
            {
                while (!empty() && current->name <= name) {
                    ++current;
                }
            }
        };

        /** Ordering for a min-heap over (name, priority) via the std max-heap algorithms. */
        static bool after(const Cursor & a, const Cursor & b) noexcept
        {
            if (a.current->name != b.current->name) {
                return a.current->name > b.current->name;
            }
            return a.priority > b.priority;
        }

        boost::container::static_vector<Cursor, MAX_LAYERS> heap;
        pointer current = nullptr;
        /** Single-layer sets iterate their array directly, skipping the heap. */
        bool merging = false;

        void push(Cursor c) noexcept
        {
            heap.push_back(c);
            std::push_heap(heap.begin(), heap.end(), after);
        }

        Cursor pop() noexcept
        {
            std::pop_heap(heap.begin(), heap.end(), after);
            auto c = heap.back();
            heap.pop_back();
            return c;
        }

        void emit(Cursor c) noexcept
        {
            current = c.current;
            ++c.current;
            if (!c.empty()) {
                push(c);
            }
        }

        explicit iterator(const Bindings & b) noexcept : merging(b.baseLayer != nullptr)
        {
            if (!merging) {
                if (b.numAttrs == 0) {
                    return;
                }
                current = b.attrs;
                heap.push_back(Cursor{b.attrs, b.attrs + b.numAttrs, 0});
                return;
            }

            uint32_t priority = 0;
            for (auto * layer = &b; layer; layer = layer->baseLayer) {
                if (layer->numAttrs != 0) {
                    push(Cursor{layer->attrs, layer->attrs + layer->numAttrs, priority});
                }
                priority++;
            }
            if (!heap.empty()) {
                emit(pop());
            }
        }

    public:
        iterator() = default;

        reference operator*() const noexcept
        {
            return *current;
        }
        pointer operator->() const noexcept
        {
            return current;
        }

        iterator & operator++() noexcept
        {
            if (!merging) {
                ++current;
                if (current == heap.front().end) {
                    current = nullptr;
                }
                return *this;
            }

            /* Drop every cursor entry whose name we already emitted; those
               are shadowed by the higher-priority layer that won. */
            Symbol emitted = current->name;
            while (!heap.empty() && heap.front().current->name <= emitted) {
                auto c = pop();
                c.skipThrough(emitted);
                if (!c.empty()) {
                    push(c);
                }
            }

            if (heap.empty()) {
                current = nullptr;
                return *this;
            }
            emit(pop());
            return *this;
        }

        iterator operator++(int) noexcept
        {
            iterator tmp = *this;
            ++*this;
            return tmp;
        }

        bool operator==(const iterator & rhs) const noexcept
        {
            return current == rhs.current;
        }
    };

    using const_iterator = iterator;

    void push_back(const Attr & attr)
    {
        attrs[numAttrs++] = attr;
        numAttrsInChain = numAttrs;
    }

    const Attr * get(Symbol name) const noexcept
    {
        for (auto * layer = this; layer; layer = layer->baseLayer) {
            auto first = layer->attrs;
            auto last = first + layer->numAttrs;
            auto i = std::lower_bound(first, last, name, [](const Attr & a, const Symbol & s) {
                return a.name < s;
            });
            if (i != last && i->name == name) {
                return i;
            }
        }
        return nullptr;
    }

    bool isLayerListFull() const noexcept
    {
        return numLayers == MAX_LAYERS;
    }

    bool isLayered() const noexcept
    {
        return numLayers > 1;
    }

    iterator begin() const
    {
        return iterator(*this);
    }
    iterator end() const
    {
        return iterator();
    }

    /**
     * Positional access is only meaningful for a flat (unlayered) set,
     * e.g. one the caller just built itself.
     */
    Attr & operator[](Size pos)
    {
        assert(!isLayered());
        return attrs[pos];
    }

    void sort();

    /**
     * Returns the attributes in lexicographically sorted order.
     */
    std::vector<const Attr *> lexicographicOrder(const SymbolTable & symbols) const
    {
        std::vector<const Attr *> res;
        res.reserve(size());
        for (auto & a : *this) {
            res.emplace_back(&a);
        }
        // NOTE: this sort uses a total order, so the iteration over pointers is not an issue
        // NOLINTNEXTLINE(bugprone-nondeterministic-pointer-iteration-order)
        std::sort(res.begin(), res.end(), [&](const Attr * a, const Attr * b) {
            std::string_view sa = symbols[a->name], sb = symbols[b->name];
            return sa < sb;
        });
        return res;
    }

    friend class EvalMemory;
};

/**
 * A wrapper around Bindings that ensures that its always in sorted
 * order at the end. The only way to consume a BindingsBuilder is to
 * call finish(), which sorts the bindings.
 */
class BindingsBuilder
{
public:
    using Size = Bindings::Size;

private:
    Bindings * bindings;
    SymbolTable & symbols;
    Size capacity;

    /**
     * A layered set's user-visible size is |base| + |own attrs| - |names in
     * both|. The rhs is small by construction (the layering threshold), so
     * either merge-walk both when the base is smaller, or binary-search the
     * base per own attr, to avoid iterating a large base.
     */
    void finishSizeIfLayered()
    {
        if (!bindings->baseLayer) {
            return;
        }

        auto & base = *bindings->baseLayer;
        auto own = std::span(bindings->attrs, bindings->numAttrs);
        Size duplicates = 0;

        if (own.size() > base.size()) {
            auto b = base.begin();
            auto o = own.begin();
            while (b != base.end() && o != own.end()) {
                if (b->name < o->name) {
                    ++b;
                } else if (o->name < b->name) {
                    ++o;
                } else {
                    ++duplicates;
                    ++b;
                    ++o;
                }
            }
        } else {
            for (auto & a : own) {
                if (base.get(a.name)) {
                    ++duplicates;
                }
            }
        }

        bindings->numAttrsInChain = base.numAttrsInChain + own.size() - duplicates;
    }

public:
    // needed by std::back_inserter
    using value_type = Attr;

    BindingsBuilder(SymbolTable & symbols, Bindings * bindings, Size capacity)
        : bindings(bindings)
        , symbols(symbols)
        , capacity(capacity)
    {
    }

    void insert(Symbol name, Value value, PosIdx pos = noPos)
    {
        insert(Attr(name, value, pos));
    }

    void insert(std::string_view name, Value value, PosIdx pos = noPos);

    void insert(const Attr & attr)
    {
        push_back(attr);
    }

    void push_back(const Attr & attr)
    {
        assert(bindings->numAttrs < capacity);
        bindings->push_back(attr);
    }

    /**
     * Make the set under construction shadow `base` instead of copying it.
     * Must be called before any attrs are added.
     */
    void layerOnTopOf(const Bindings & base) noexcept
    {
        assert(bindings->numAttrs == 0 && bindings != &Bindings::EMPTY);
        bindings->baseLayer = &base;
        bindings->numLayers = base.numLayers + 1;
    }

    [[nodiscard("must use created bindings")]]
    Bindings * finish()
    {
        bindings->sort();
        finishSizeIfLayered();
        return bindings;
    }

    Bindings * alreadySorted()
    {
        finishSizeIfLayered();
        return bindings;
    }
};

}
