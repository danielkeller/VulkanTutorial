#ifndef util_h
#define util_h

inline void throwFail(const char *context, const vk::Result &result) {
  if (result != vk::Result::eSuccess)
    vk::throwResultException(result, "context");
}

template <class T>
struct BufferRef {
  BufferRef(char* buffer, size_t stride) : buffer_(buffer), stride_(stride){};
  template <class U>
  BufferRef(U* arr, T U::*member)
      : buffer_((char*)&(arr->*member)), stride_(sizeof(U)) {}
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

#endif /* util_h */
