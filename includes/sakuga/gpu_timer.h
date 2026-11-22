#ifndef GPU_TIMER_H
#define GPU_TIMER_H

#include <glad/glad.h>

// Measures how long the GPU spends on named zones of a frame (GL_TIME_ELAPSED queries).
//
//   timer.nextFrame();                      // once at the start of every frame
//   timer.begin(ZONE_SHADOW); ...draw...; timer.end(ZONE_SHADOW);
//   float ms = timer.ms(ZONE_SHADOW);       // smoothed, a few frames old
//
// Results are read LATENCY frames after they were issued, so reading never stalls the pipeline.
// GL allows only one GL_TIME_ELAPSED query at a time: zones must run one after another, never nested.
// A zone that wasn't issued in a frame (e.g. a cached shadow pass) counts as 0 ms for that frame.
class GpuTimer
{
public:
    static constexpr int MAX_ZONES = 8;
    static constexpr int LATENCY = 3;

    bool enabled = true;

    explicit GpuTimer(int zoneCount) : zones(zoneCount < MAX_ZONES ? zoneCount : MAX_ZONES)
    {
        for (int z = 0; z < zones; ++z)
            glGenQueries(LATENCY, queries[z]);
    }
    ~GpuTimer() { release(); }

    GpuTimer(const GpuTimer&) = delete;
    GpuTimer& operator=(const GpuTimer&) = delete;

    void release()
    {
        for (int z = 0; z < zones; ++z)
            if (queries[z][0]) { glDeleteQueries(LATENCY, queries[z]); queries[z][0] = 0; }
    }

    void nextFrame()
    {
        current = (current + 1) % LATENCY;   // this slot was last used LATENCY frames ago
        for (int z = 0; z < zones; ++z)
        {
            if (!queries[z][0])
                continue;
            float sample = 0.0f;
            if (issued[z][current])
            {
                GLuint available = 0;
                glGetQueryObjectuiv(queries[z][current], GL_QUERY_RESULT_AVAILABLE, &available);
                if (available)
                {
                    GLuint64 ns = 0;
                    glGetQueryObjectui64v(queries[z][current], GL_QUERY_RESULT, &ns);
                    sample = static_cast<float>(ns) * 1e-6f;
                }
                else
                    sample = smoothed[z];    // not ready yet: keep the last value instead of stalling
            }
            smoothed[z] += (sample - smoothed[z]) * 0.1f;
            issued[z][current] = false;
        }
    }

    void begin(int zone)
    {
        if (!enabled || zone >= zones || !queries[zone][0]) return;
        glBeginQuery(GL_TIME_ELAPSED, queries[zone][current]);
        issued[zone][current] = true;
    }

    void end(int zone)
    {
        if (!enabled || zone >= zones || !queries[zone][0] || !issued[zone][current]) return;
        glEndQuery(GL_TIME_ELAPSED);
    }

    float ms(int zone) const { return (zone < zones) ? smoothed[zone] : 0.0f; }

private:
    int zones;
    int current = 0;
    unsigned int queries[MAX_ZONES][LATENCY] = {};
    bool issued[MAX_ZONES][LATENCY] = {};
    float smoothed[MAX_ZONES] = {};
};

#endif