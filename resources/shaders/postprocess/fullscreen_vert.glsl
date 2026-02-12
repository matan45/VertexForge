layout(location = 0) out vec2 texCoord;

void main()
{
    // Fullscreen triangle: 3 vertices cover entire NDC [-1,1] range
    // Vertex 0: (-1, -1)  UV (0, 1)
    // Vertex 1: ( 3, -1)  UV (2, 1)
    // Vertex 2: (-1,  3)  UV (0,-1)
    vec2 pos = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    texCoord = pos;
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
