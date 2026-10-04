#include "rendering/MeshLoader.h"
#include "core/CrashHandler.h"
#include "external/ufbx.h"
#include <glm/glm.hpp>
#include <iostream>
#include <vector>

namespace Forge {

std::unique_ptr<Mesh> MeshLoader::loadFromFile(VulkanContext* context, const std::string& filepath) {
    CrashHandler::logInfo("Loading mesh from: " + filepath);
    
    // Load the scene using ufbx
    ufbx_load_opts opts = {};
    opts.target_axes.right = UFBX_COORDINATE_AXIS_POSITIVE_X;
    opts.target_axes.up = UFBX_COORDINATE_AXIS_POSITIVE_Y;
    opts.target_axes.front = UFBX_COORDINATE_AXIS_POSITIVE_Z;
    
    ufbx_error error;
    ufbx_scene* scene = ufbx_load_file(filepath.c_str(), &opts, &error);
    
    if (!scene) {
        std::string errorMsg = "Failed to load mesh: " + filepath + " - Error: " + std::string(error.description.data);
        std::cerr << errorMsg << std::endl;
        CrashHandler::logError(errorMsg);
        return nullptr;
    }
    
    // Find the first mesh in the scene
    if (scene->meshes.count == 0) {
        std::string errorMsg = "No meshes found in file: " + filepath;
        std::cerr << errorMsg << std::endl;
        CrashHandler::logError(errorMsg);
        ufbx_free_scene(scene);
        return nullptr;
    }
    
    ufbx_mesh* fbx_mesh = scene->meshes.data[0];
    
    // Extract vertex data
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    
    // Reserve space based on number of indices
    size_t num_indices = fbx_mesh->num_indices;
    vertices.reserve(num_indices);
    indices.reserve(num_indices);
    
    // Process each face
    for (size_t fi = 0; fi < fbx_mesh->num_faces; fi++) {
        ufbx_face face = fbx_mesh->faces.data[fi];
        
        // Triangulate polygons (simple fan triangulation)
        for (uint32_t ci = 2; ci < face.num_indices; ci++) {
            // Triangle: 0, ci-1, ci
            uint32_t triangle_indices[3] = {
                face.index_begin,
                face.index_begin + ci - 1,
                face.index_begin + ci
            };
            
            for (int ti = 0; ti < 3; ti++) {
                uint32_t index = triangle_indices[ti];
                
                Vertex vertex;
                
                // Get position
                ufbx_vec3 pos = ufbx_get_vertex_vec3(&fbx_mesh->vertex_position, index);
                vertex.position = glm::vec3((float)pos.x, (float)pos.y, (float)pos.z);
                
                // Get normal
                if (fbx_mesh->vertex_normal.exists) {
                    ufbx_vec3 normal = ufbx_get_vertex_vec3(&fbx_mesh->vertex_normal, index);
                    vertex.normal = glm::vec3((float)normal.x, (float)normal.y, (float)normal.z);
                } else {
                    // Default normal
                    vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
                }
                
                // Get UV
                if (fbx_mesh->vertex_uv.exists) {
                    ufbx_vec2 uv = ufbx_get_vertex_vec2(&fbx_mesh->vertex_uv, index);
                    vertex.texCoords = glm::vec2((float)uv.x, (float)uv.y);
                } else {
                    // Default UV
                    vertex.texCoords = glm::vec2(0.0f, 0.0f);
                }
                
                vertices.push_back(vertex);
                indices.push_back((unsigned int)indices.size());
            }
        }
    }
    
    // Clean up ufbx scene
    ufbx_free_scene(scene);
    
    // Create and return the Mesh object
    auto mesh = std::make_unique<Mesh>(context, vertices, indices);
    
    CrashHandler::logInfo("Loaded mesh: " + filepath + " - Vertices: " + std::to_string(vertices.size()) + ", Triangles: " + std::to_string(indices.size() / 3));
    
    return mesh;
}

} // namespace Forge
