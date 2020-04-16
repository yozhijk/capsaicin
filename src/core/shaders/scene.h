#ifndef SCENE_H
#define SCENE_H

struct Mesh
{
    uint vertex_count;
    uint first_vertex_offset;
    uint index_count;
    uint first_index_offset;

    uint index;
    uint texture_index;
    uint2 padding;
};
 

#endif // SCENE_H