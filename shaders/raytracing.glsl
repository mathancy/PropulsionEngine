// Shared by the ray-traced passes: how a shader finds the triangle a ray hit.
// The instance table is written by RayTracing::update (include/rendering/RayTracing.h, struct GpuRtInstance: keep in step).
// Include after: #version 460 and the extensions below.
#extension GL_EXT_ray_query : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

struct RtInstance {
    uint64_t vertexAddress;
    uint64_t indexAddress;
    mat3 normalMatrix;      // object normals to world normals
    uint materialIndex;
};

// Same layout as Vertex in include/rendering/Mesh.h
struct RtVertex {
    vec3 position;
    vec3 normal;
    vec2 texCoords;
};

layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer RtVertices { RtVertex v[]; };
layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer RtIndices { uint i[]; };

// The three vertices of triangle `primitive` of the instance's mesh.
void rtTriangle(RtInstance instance, uint primitive, out RtVertex a, out RtVertex b, out RtVertex c) {
    RtIndices indices = RtIndices(instance.indexAddress);
    RtVertices vertices = RtVertices(instance.vertexAddress);
    a = vertices.v[indices.i[primitive * 3u + 0u]];
    b = vertices.v[indices.i[primitive * 3u + 1u]];
    c = vertices.v[indices.i[primitive * 3u + 2u]];
}

// Barycentric weights of the three vertices from the two the ray query reports.
vec3 rtBarycentrics(vec2 hit) {
    return vec3(1.0 - hit.x - hit.y, hit.x, hit.y);
}

// The smooth (interpolated) world-space normal at a hit.
vec3 rtWorldNormal(RtInstance instance, uint primitive, vec2 hit) {
    RtVertex a, b, c;
    rtTriangle(instance, primitive, a, b, c);
    const vec3 w = rtBarycentrics(hit);
    return normalize(instance.normalMatrix * (a.normal * w.x + b.normal * w.y + c.normal * w.z));
}
