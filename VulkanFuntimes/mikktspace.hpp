#ifndef mikktspace_h
#define mikktspace_h

#include "glm/vec2.hpp"
#include "glm/vec3.hpp"
#include "glm/vec4.hpp"

template <class T>
struct BufferRef {
  BufferRef(char* buffer, size_t stride) : buffer_(buffer), stride_(stride){};
  T& operator[](size_t i) { return *(T*)(buffer_ + stride_ * i); }
  char* buffer_;
  size_t stride_;
};
template <class T>
struct BufferRef<const T> {
  BufferRef(const char* buffer, size_t stride)
      : buffer_(buffer), stride_(stride){};
  BufferRef(const BufferRef<T>& other)
      : BufferRef(other.buffer_, other.stride_){};
  const T& operator[](size_t i) { return *(T*)(buffer_ + stride_ * i); }
  const char* buffer_;
  size_t stride_;
};

void makeTangents(uint32_t nIndices, uint16_t* indices,
                  BufferRef<const glm::vec3> positions,
                  BufferRef<const glm::vec3> normals,
                  BufferRef<const glm::vec2> texCoords,
                  BufferRef<glm::vec4> tangents);

#endif /* mikktspace_h */
