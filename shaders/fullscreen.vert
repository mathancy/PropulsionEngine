#version 450

// Fullscreen triangle vertex shader
// Draws a fullscreen triangle without any vertex buffer
// Uses vertex ID to generate positions

layout(location = 0) out vec2 fragTexCoord;

void main() {
    // Generate fullscreen triangle vertices from gl_VertexIndex
    // Vertex 0: (-1, -1), Vertex 1: (3, -1), Vertex 2: (-1, 3)
    vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );
    
    // UV coordinates: (0,0), (2,0), (0,2)
    vec2 texCoords[3] = vec2[](
        vec2(0.0, 0.0),
        vec2(2.0, 0.0),
        vec2(0.0, 2.0)
    );
    
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    fragTexCoord = texCoords[gl_VertexIndex];
}
