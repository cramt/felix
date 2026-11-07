# Exercises `//` results stored as layers (felix): shadowing, size with
# duplicate names, chains past the layer cap, rhs above the layering
# threshold, and every consumer that iterates or indexes a set.
let
  base = builtins.listToAttrs (map (n: { name = "k${toString n}"; value = n; }) (builtins.genList (x: x) 40));
  step = acc: n: acc // { "k${toString n}" = n * 100; "new${toString n}" = n; };
  # 12 small updates: crosses the 8-layer cap, so some get flattened
  deep = builtins.foldl' step base (builtins.genList (x: x) 12);
  big = builtins.listToAttrs (map (n: { name = "b${toString n}"; value = n; }) (builtins.genList (x: x) 20));
  layeredRhs = { a = 1; } // { b = 2; } // { a = 3; };
  flat = builtins.listToAttrs (map (name: { inherit name; value = deep.${name}; }) (builtins.attrNames deep));
in
{
  names = builtins.attrNames deep;
  size = builtins.length (builtins.attrNames deep);
  shadowed = [ deep.k0 deep.k5 deep.k11 deep.k12 deep.k39 ];
  has = [ (deep ? k3) (deep ? new11) (deep ? new12) ];
  values = builtins.attrValues deep;
  equalToFlat = deep == flat;
  bigRhs = builtins.attrNames (deep // big);
  rhsIsLayered = (base // layeredRhs).a;
  emptyEdges = [ ((deep // { }) == deep) (({ } // deep) == deep) ];
  removed = builtins.attrNames (builtins.removeAttrs deep [ "k0" "new3" "k39" ]);
  intersected = builtins.intersectAttrs { k1 = null; new2 = null; zz = null; } deep;
  mapped = builtins.attrValues (builtins.mapAttrs (n: v: v + 1) ({ x = 1; } // { y = 2; x = 3; }));
  json = builtins.toJSON ({ c = 1; a = 2; } // { b = 3; a = 4; });
  withScope = with ({ p = 1; } // { q = 2; p = 3; }); p + q;
  inherited = let inherit (base // { k1 = "over"; }) k1 k2; in [ k1 k2 ];
  fnArgs = (({ a, b ? 0, ... }@args: [ a b (args ? z) ]) ({ a = 1; z = 0; } // { b = 2; }));
  catted = builtins.catAttrs "v" [ ({ v = 1; } // { v = 2; }) { v = 3; } ];
  lazyLower = (({ boom = throw "lower layer must stay lazy"; } // { ok = 1; }).ok);
}
