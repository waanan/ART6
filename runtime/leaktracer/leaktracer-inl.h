/* -*- mode: c++ -*-
 * Copyright (C) 2015 Yu Hengyang.
 *
 * leaktracer-inl.h
 *
 * History:
 *
 * Created on 2015-11-17 by Hengyang Yu.
 *   Initial craft.
 */
#ifndef ART_RUNTIME_LEAKTRACER_LEAKTRACER_INL_H_
#define ART_RUNTIME_LEAKTRACER_LEAKTRACER_INL_H_

namespace art {
  namespace leaktracer {

    const int kAccessBit = 0x1;

    template <typename T>
    inline T* ClearAccessBit(T *klass) {
      uintptr_t raw = reinterpret_cast<uintptr_t>(klass);
      raw &= ~kAccessBit;
      return reinterpret_cast<T*>(raw);
    }

    inline void SetAccessBit(void *obj) {
      uintptr_t *rawp = static_cast<uintptr_t*>(obj);
      *rawp |= kAccessBit;
    }


    inline bool Accessed(void *obj) {
      // get klass_ pointer of obj
      uintptr_t raw = *static_cast<uintptr_t*>(obj);
      return raw & kAccessBit;
    }

}  // namespace leaktracer
}  // namespace art

#endif  // ART_RUNTIME_LEAKTRACER_LEAKTRACER_INL_H_
