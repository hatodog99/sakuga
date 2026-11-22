#ifndef UNIFORM_BUFFER_H
#define UNIFORM_BUFFER_H

#include <glad/glad.h>

#include <utility>

// A GL uniform buffer holding one struct T, bound to a fixed binding point for its whole life.
// T must follow std140 layout (only mat4 / vec4 / ivec4 members, see shader_config.h).
// Move-only: copying would copy the GL id and double-delete it.
// release() is idempotent; call it before the GL context goes away (the destructor calls it too).
template <typename T>
class UniformBuffer
{
public:
    explicit UniformBuffer(unsigned int bindingPoint) : binding(bindingPoint)
    {
        glGenBuffers(1, &id);
        glBindBuffer(GL_UNIFORM_BUFFER, id);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(T), nullptr, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER, binding, id);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }
    ~UniformBuffer() { release(); }

    UniformBuffer(const UniformBuffer&) = delete;
    UniformBuffer& operator=(const UniformBuffer&) = delete;
    UniformBuffer(UniformBuffer&& o) noexcept : id(std::exchange(o.id, 0)), binding(o.binding) {}
    UniformBuffer& operator=(UniformBuffer&& o) noexcept
    {
        if (this != &o)
        {
            release();
            id = std::exchange(o.id, 0);
            binding = o.binding;
        }
        return *this;
    }

    // uploads the whole struct (call once per frame, not per draw)
    void update(const T& data) const
    {
        glBindBuffer(GL_UNIFORM_BUFFER, id);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(T), &data);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }

    void release()
    {
        if (id) { glDeleteBuffers(1, &id); id = 0; }
    }

    unsigned int bindingPoint() const { return binding; }

private:
    unsigned int id = 0;
    unsigned int binding = 0;
};

#endif