in in_frag_color : vec3<f32> at 0
out out_color : vec4<f32> at 0

[entry_point(fragment)]
procedure frag() -> void
    out_color := {in_frag_color, 1.0};
end
