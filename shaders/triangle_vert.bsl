in in_pos : vec3<f32> at 0
in in_normal : vec3<f32> at 1

out out_color : vec4<f32> at 0

[builtin(position)]
out out_position: vec4<f32>

[entry_point(vertex)]
procedure vert() -> void
  out_color := {in_normal + {0.9, 0.6, 0.3}, 1.0} * 0.8;
  out_position := {in_pos, 1.0};
end
