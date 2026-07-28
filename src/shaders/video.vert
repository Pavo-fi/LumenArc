#version 440

layout(location = 0) out vec2 v_uv;
layout(std140, binding = 0) uniform Buf { mat4 u_clip; };

// 全屏三角形：D3D11 纹理行 0 在顶部，NDC y 向上，故翻转 uv.y
void main()
{
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    v_uv = vec2(p.x * 0.5, 1.0 - p.y * 0.5);
    gl_Position = u_clip * vec4(p - 1.0, 0.0, 1.0);
}
