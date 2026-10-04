#version 450

layout(location = 0) in vec3 nearPoint;
layout(location = 1) in vec3 farPoint;
layout(location = 2) in mat4 fragView;
layout(location = 6) in mat4 fragProj;

layout(location = 0) out vec4 FragColor;

layout(push_constant) uniform GridParams {
    vec4 gridColor;
    vec4 axisXColor;
    vec4 axisYColor;
    vec4 cameraPos;
    float gridSpacing;
    float fadeDistance;
    float viewportHeight;  // Need this for adaptive scale
    float fov;             // Field of view in radians
} params;

float computeDepth(vec3 pos) {
    vec4 clipSpacePos = fragProj * fragView * vec4(pos, 1.0);
    // Vulkan depth range is 0 to 1, no conversion needed
    return clipSpacePos.z / clipSpacePos.w;
}

// Helper function to render grid at a specific scale
// Grid is on XY plane (Z=0) for Z-up coordinate system
vec4 renderGridAtScale(vec3 fragPos3D, float gridStep) {
    // Compute grid coordinates on XY plane
    vec2 coord = fragPos3D.xy / gridStep;
    vec2 derivative = fwidth(coord);
    
    // Clamp derivative to prevent lines from disappearing when very close
    derivative = max(derivative, vec2(0.0001));
    
    vec2 grid = abs(fract(coord - 0.5) - 0.5) / derivative;
    float line = min(grid.x, grid.y);
    float minorLine = line < 1.0 ? 1.0 : 0.0;
    
    // Return with premultiplied alpha
    vec3 gridColor = vec3(1.0, 1.0, 1.0);
    float alpha = minorLine * 0.3;
    return vec4(gridColor * alpha, alpha);
}

// Blender-style infinite grid with adaptive scale and smooth transitions
vec4 grid(vec3 fragPos3D) {
    // Calculate adaptive scale based on distance
    float distanceToGrid = length(fragPos3D - params.cameraPos.xyz);
    float unitsPerPixel = distanceToGrid / tan(params.fov * 0.5) / params.viewportHeight;
    
    // Ensure unitsPerPixel has a reasonable minimum value
    unitsPerPixel = max(unitsPerPixel, 0.001);
    
    // Logarithmic scale detection with smooth transition
    float logScale = log(unitsPerPixel * 20.0) / log(10.0);
    float logFloor = floor(logScale);
    float logFract = fract(logScale); // 0.0 to 1.0 between scale transitions
    
    // Current and next grid steps (always at least 1.0 unit spacing)
    float gridStep1 = max(1.0, pow(10.0, logFloor));
    float gridStep2 = gridStep1 * 10.0;
    
    // ALWAYS render at the base 1.0 unit scale when very close to ensure visibility
    // This prevents disappearing grid when camera is low
    if (distanceToGrid < 30.0) {
        return renderGridAtScale(fragPos3D, 1.0);
    }
    
    // Render grid at both scales and blend between them
    vec4 grid1 = renderGridAtScale(fragPos3D, gridStep1);
    vec4 grid2 = renderGridAtScale(fragPos3D, gridStep2);
    
    // Crossfade with guaranteed minimum visibility
    // These ranges overlap significantly to prevent any gaps
    float fade1 = 1.0 - smoothstep(0.2, 0.8, logFract);
    float fade2 = smoothstep(0.2, 0.8, logFract);
    
    // Ensure at least one scale is always fully visible
    float maxFade = max(fade1, fade2);
    if (maxFade < 0.8) {
        float boost = 0.8 / maxFade;
        fade1 *= boost;
        fade2 *= boost;
    }
    
    // Blend the two grid scales (already premultiplied from renderGridAtScale)
    return grid1 * fade1 + grid2 * fade2;
}

// Render axis lines separately from grid
// Grid is on XY plane (Z=0) for Z-up coordinate system
vec4 renderAxes(vec3 fragPos3D) {
    // Calculate screen-space derivatives for axis thickness
    vec2 coordForAxis = fragPos3D.xy / 1.0;
    vec2 derivativeAxis = fwidth(coordForAxis);
    
    // Clamp derivative to prevent axes from disappearing when very close
    derivativeAxis = max(derivativeAxis, vec2(0.0001));
    
    // X axis: red (where Y = 0, along X direction)
    float xAxisDist = abs(fragPos3D.y) / derivativeAxis.y;
    float xAxis = xAxisDist < 1.5 ? 1.0 : 0.0;
    if (xAxis > 0.01) {
        vec3 xAxisColor = vec3(0.949, 0.235, 0.235);
        float xAxisAlpha = xAxis * 0.9;
        return vec4(xAxisColor * xAxisAlpha, xAxisAlpha);
    }
    
    // Y axis: green (where X = 0, along Y direction)
    float yAxisDist = abs(fragPos3D.x) / derivativeAxis.x;
    float yAxis = yAxisDist < 1.5 ? 1.0 : 0.0;
    if (yAxis > 0.01) {
        vec3 yAxisColor = vec3(0.420, 0.859, 0.357);
        float yAxisAlpha = yAxis * 0.9;
        return vec4(yAxisColor * yAxisAlpha, yAxisAlpha);
    }
    
    return vec4(0.0);
}

void main() {
    // Find intersection with Z=0 plane (XY floor plane for Z-up coordinate system)
    // Ray: P = nearPoint + t * (farPoint - nearPoint)
    // At Z=0: nearPoint.z + t * (farPoint.z - nearPoint.z) = 0
    // t = -nearPoint.z / (farPoint.z - nearPoint.z)
    float t = -nearPoint.z / (farPoint.z - nearPoint.z);
    
    if (t < 0.0) {
        discard;
    }
    
    vec3 fragPos3D = nearPoint + t * (farPoint - nearPoint);
    
    gl_FragDepth = computeDepth(fragPos3D);
    
    // Draw grid
    vec4 gridResult = grid(fragPos3D);
    
    // Draw axes on top
    vec4 axisResult = renderAxes(fragPos3D);
    
    // Axes override grid (axes drawn on top)
    if (axisResult.a > 0.01) {
        gridResult = axisResult;
    }
    
    // Distance fade with discard for distant fragments
    float distanceFromCamera = length(fragPos3D - params.cameraPos.xyz);
    
    // Hard cutoff at maximum distance
    if (distanceFromCamera > params.fadeDistance) {
        discard;
    }
    
    // Gradual fade in the outer region (70-100% of fadeDistance)
    float fadeStart = params.fadeDistance * 0.7;
    float fadeFactor = 1.0 - smoothstep(fadeStart, params.fadeDistance, distanceFromCamera);
    gridResult.a *= fadeFactor;
    
    // Discard when alpha gets too low
    if (gridResult.a < 0.001) {
        discard;
    }
    
    // Use premultiplied alpha for proper transparency
    gridResult.rgb *= gridResult.a;
    
    FragColor = gridResult;
}
