#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in float fragAlpha;

layout(location = 0) out vec4 outColor;

void main() {
    // Flip normal for back faces to achieve double-sided lighting
    vec3 normal = gl_FrontFacing ? normalize(fragNormal) : -normalize(fragNormal);
    
    // Simple flat shading for gizmo with high ambient to keep torus rings bright
    vec3 lightDir = normalize(vec3(0.5, 0.5, 1.0));
    float diffuse = max(dot(normal, lightDir), 0.55);
    outColor = vec4(fragColor * diffuse, fragAlpha);
}
