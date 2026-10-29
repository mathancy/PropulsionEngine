#version 450

// A full-screen triangle, no vertex buffer. Passes the position on the screen (-1..1) so the fragment shader can
// work out which way each pixel looks.

layout(location = 0) out vec2 ndc;

void main() {
    vec2 positions[3] = vec2[](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    ndc = positions[gl_VertexIndex];
    gl_Position = vec4(ndc, 0.0, 1.0);
}
