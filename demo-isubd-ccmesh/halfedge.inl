////////////////////////////////////////////////////////////////////////////////
// Halfedge Mesh Loader
//
////////////////////////////////////////////////////////////////////////////////
template <int N>
struct halfedge {
    struct vertex { float x, y, z, w; };
    struct edge { int v0, ef, eb, en; }; /* vertex + next, previous, and neighbour edges */
    std::vector<vertex> vbuf;
    std::vector<edge> ebuf;

    explicit halfedge(const char *path_to_obj);
private:
    // OBJ File
    struct objloader {
        std::vector<vertex> vbuf;
        std::vector<uint32_t> fbuf;

        objloader(const char *filename);
    };
    // Conversion
    halfedge(const objloader &obj);
};
typedef halfedge<3> halfedge3;
typedef halfedge<4> halfedge4;

template <int N>
halfedge<N>::halfedge(const char *path_to_obj):
    halfedge(halfedge::objloader(path_to_obj))
{}

template <int N>
halfedge<N>::objloader::objloader(const char *filename)
{
    FILE *pf = fopen(filename, "r");
    char buf[1024];
    float v[3], vmin[3] = {1e5}, vmax[3] = {-1e5};
    int dummy, f;

    while(fscanf(pf, "%s", buf) != EOF) {
        switch(buf[0]) {
        case 'v':
            switch(buf[1]) {
            case '\0':
                fscanf(pf, "%f %f %f", &v[0], &v[1], &v[2]);
                for (int i = 0; i < 3; ++i) {
                    vmin[i] = std::min(vmin[i], v[i]);
                    vmax[i] = std::max(vmax[i], v[i]);
                }
                vbuf.push_back({v[0], v[1], v[2], 1.0f});
            break;
            default:
                fgets(buf, sizeof(buf), pf);
            break;
            }
        break;

        case 'f':
            f = 0;
            fscanf(pf, "%s", buf);
            if (strstr(buf, "//")) {
                /* v//n */
                sscanf(buf, "%d//%d", &f, &dummy);
                --f;
                fbuf.push_back(f);
                for (int i = 0; i < N - 1; ++i) {
                    fscanf(pf, "%d//%d", &f, &dummy);
                    --f;
                    fbuf.push_back(f);
                }
            } else if (sscanf(buf, "%d/%d/%d", &f, &dummy, &dummy) == 3) {
                /* v/t/n */
                --f;
                fbuf.push_back(f);
                for (int i = 0; i < N - 1; ++i) {
                    fscanf(pf, "%d/%d/%d", &f, &dummy, &dummy);
                    --f;
                    fbuf.push_back(f);
                }
            } else if (sscanf(buf, "%d/%d", &f, &dummy) == 2) {
                /* v/t */
                --f;
                fbuf.push_back(f);
                for (int i = 0; i < N - 1; ++i) {
                    fscanf(pf, "%d/%d", &f, &dummy);
                    --f;
                    fbuf.push_back(f);
                }
            } else if (sscanf(buf, "%d", &f) == 1) {
                /* v */
                --f;
                fbuf.push_back(f);
                for (int i = 0; i < N - 1; ++i) {
                    fscanf(pf, "%d", &f);
                    --f;
                    fbuf.push_back(f);
                }
            }
            break;

        default:
            fgets(buf, sizeof(buf), pf);
        break;
        }
    }

    fclose(pf);

    // unitize
    float s1 = vmax[0] - vmin[0];
    float s2 = vmax[1] - vmin[1];
    float s3 = vmax[2] - vmin[2];
    float s = 1.0f / std::max(s3, std::max(s1, s2));
    for (int i = 0; i < (int)vbuf.size(); ++i) {
        vbuf[i].x = (vbuf[i].x - vmin[0]) * s;
        vbuf[i].y = (vbuf[i].y - vmin[1]) * s;
        vbuf[i].z = (vbuf[i].z - vmin[2]) * s;
    }
}

template <int N>
halfedge<N>::halfedge(const halfedge<N>::objloader &obj)
{
    // get number of vertices and faces
    const int vertexCount = (int)obj.vbuf.size();
    const int faceCount = (int)obj.fbuf.size() / N;
    std::map<int, int> hashMap;

    // get all vertices
    vbuf = obj.vbuf;

    // build topology
    for (int i = 0; i < faceCount; ++i) {
        int edgeCount = i * N;

        for (int j = 0; j < N; ++j) {
            int v0 = obj.fbuf[edgeCount + j];
            int v1 = obj.fbuf[edgeCount + ((j + 1) % N)];
            edge e = {
                v0,
                edgeCount + ((j + 1) % N),
                edgeCount + (((j - 1) % N + N) % N),
                -1
            };
            int edgeIndex = edgeCount + j;
            int hashID = v0 + vertexCount * v1;
            auto it = hashMap.find(hashID);

            if (/* neighbour in ebuf */ it != hashMap.end()) {
                int neighbourEdgeIndex = it->second;

                e.en = neighbourEdgeIndex;
                ebuf[neighbourEdgeIndex].en = edgeIndex;
            } else /* neighbour not in ebuf */ {
                int hashID = v1 + vertexCount * v0;

                hashMap.insert(std::pair<int, int>(hashID, edgeIndex));
            }

            ebuf.push_back(e);
        }
    }
}
