in in_position : vec3<f32> at 0
in in_normal : vec3<f32> at 1

out out_frag_color : vec3<f32> at 0

[builtin(position)]
out position : vec4<f32>

[entry_point(vertex)]
procedure vert() -> void
      position := {in_position, 1.0};
      out_frag_color := (in_normal + {0.3, 0.3, 0.3}) * 0.5;
end