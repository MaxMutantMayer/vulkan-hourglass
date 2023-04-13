#version 450

layout(constant_id = 0) const uint GRID_WIDTH = 64;
layout(constant_id = 1) const uint GRID_HEIGHT = 64;

layout(location = 0) in vec2 inUV;

layout(location = 0) out vec4 outColor;

layout(binding = 0, r32ui) uniform readonly uimageBuffer StorageTexelBuffer;

void main()
{
    int indexX = int(inUV.x * GRID_WIDTH);
    int indexY = int(inUV.y * GRID_HEIGHT);
    int index = indexX + indexY * int(GRID_WIDTH);

    uvec4 cellStateVec = imageLoad(StorageTexelBuffer, index);
    uint cellState = cellStateVec.x;

    uint redVal = cellState & 1;
    uint greenVal = redVal;
    uint blueVal = cellState & 2;

    outColor = vec4(redVal, greenVal, blueVal, 1.0);
}
