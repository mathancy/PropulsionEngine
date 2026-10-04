#version 450

layout(location = 0) in vec3 aPos;

layout(location = 0) out vec3 nearPoint;
layout(location = 1) out vec3 farPoint;
layout(location = 2) out mat4 fragView;
layout(location = 6) out mat4 fragProj;

layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 projection;
} ubo;

// Unproject point from NDC to world space
vec3 unprojectPoint(float x, float y, float z, mat4 view, mat4 projection) {
    mat4 viewInv = inverse(view);
    mat4 projInv = inverse(projection);
    vec4 unprojectedPoint = viewInv * projInv * vec4(x, y, z, 1.0);
    return unprojectedPoint.xyz / unprojectedPoint.w;
}

void main() {
    // Pass through the position for fullscreen quad
    gl_Position = vec4(aPos.xy, 0.0, 1.0);
    
    // Compute near and far points in world space
    nearPoint = unprojectPoint(aPos.x, aPos.y, 0.0, ubo.view, ubo.projection);
    farPoint = unprojectPoint(aPos.x, aPos.y, 1.0, ubo.view, ubo.projection);
    
    // Pass matrices to fragment shader
    fragView = ubo.view;
    fragProj = ubo.projection;
}
