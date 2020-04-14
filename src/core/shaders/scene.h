#ifndef SCENE_H
#define SCENE_H

struct Mesh
{
    uint vertex_count;
    uint first_vertex_offset;
    uint index_count;
    uint first_index_offset;

    uint index;
    uint padding[3];
};
 

#endif // SCENE_H