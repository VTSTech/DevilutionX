// Round-4 fixture: deliberately NOT #pragma once - the koko-aio pattern
// re-includes this file per stage region and each region needs its own copy
// of the definitions (plain textual #include semantics).
vec4 r4Tint(vec4 c) { return vec4(c.rgb * 0.5 + 0.25, c.a); }
