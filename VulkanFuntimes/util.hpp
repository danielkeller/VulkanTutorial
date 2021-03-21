#ifndef util_h
#define util_h

inline void throwFail(const char *context, const vk::Result &result) {
  if (result != vk::Result::eSuccess)
    vk::throwResultException(result, "context");
}

#endif /* util_h */
