#version 450

// Depth only: a mesh seen from a light. No fragment shader is needed.

layout(push_constant) uniform Push {
    mat4 lightViewProjection;
    mat4 model;
} pc;

layout(location = 0) in vec3 inPosition;

void main() {
    gl_Position = pc.lightViewProjection * pc.model * vec4(inPosition, 1.0);
}
