/*
 * Copyright (C) 2015 Yu Hengyang.
 *
 *
 * leaktracer.cc - Implementation of leaktracer.h.
 *
 * History:
 *
 *   Created on 2015-9-5 by Yu Hengyang, hengyyu@gmail.com
 */

#define LOG_TAG "LeakTracer"
#include <utils/Log.h>

#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <set>
#include <fstream>

#include "gc/heap.h"
#include "runtime.h"
#include "gc/space/large_object_space.h"
#include "utils.h"
#include "class_linker.h"
#include "art_method-inl.h"
#include "mirror/object_array-inl.h"
#include "mirror/class-inl.h"
#include "thread.h"
// #include <signal.h>

#include "leaktracer.h"


#define LARGE_OBJECT_SPACE_BEGIN reinterpret_cast<unsigned>(Runtime::Current()->GetHeap()->GetLargeObjectsSpace()->Begin())
#define PRINT(fp, ...) do {                     \
    if (gLogOnly) {                             \
      ALOGD(__VA_ARGS__);                       \
    } else {                                    \
      fprintf(fp, __VA_ARGS__);                 \
      fflush(fp);                               \
    }                                           \
  } while (0)

namespace art {
  namespace leaktracer {

    LeakTracer* LeakTracer::instance_ = nullptr;
    bool gLeakTracerIsTracking = false;
    bool gLogOnly = false;           // if true, do not write to file.

    std::set<std::string> gBenchmarks;
    std::set<std::string> noTrackSet;

    // static void
    // exitSigHandler(int sig)
    // {
    //   // Log quit information.
    //   ALOGD("Get quit sig from ActivityManager, SIG :  %d \n", sig);
    //   gLeakTracerIsTracking = false;
    //   auto *instance = leaktracer::LeakTracer::Instance();
    //   instance->AppFinished();
    //   delete instance;
    //   kill(getpid(),SIGKILL);
    // }

    static void InitBenchmarkSet() {
      // for benchmarks started by the ActivityManager
      std::ifstream ifs("/data/local/tmp/track");
      std::string  app_name;
      while (getline(ifs, app_name)) {
        gBenchmarks.insert(app_name);
      }
      gBenchmarks.insert("com.eembc.andebench");
      gBenchmarks.insert("com.dhry2");
      gBenchmarks.insert("com.futuremark.dmandroid.application");
      gBenchmarks.insert("com.LinpackJava");
      gBenchmarks.insert("com.LinpackSP");
      gBenchmarks.insert("com.linpackv7");
      gBenchmarks.insert("com.buaa.waanan.ltconfig");
      gBenchmarks.insert("com.android.calculator2");

      // for benchmarks started manually from the adb shell
      gBenchmarks.insert("dalvikvm");
    }

    static void InitNoTrackSet() {
      // don't track some system process, cause track them cause system error
      noTrackSet.insert("/system/bin/dex2oat");
    }

    static bool GetProcNameByPid(char *buf, size_t size, int pid) {
      snprintf(buf, size, "/proc/%d/cmdline", pid);
      FILE *fp = fopen(buf, "r");
      if (fp) {
        char tmp[1024];
        if (fread(tmp, sizeof(char), size, fp))
          strcpy(buf, tmp);
        fclose(fp);
        return true;
      }
      return false;
    }

    static void GetBenchmarkName(char *buf, size_t size, int pid) {
      // Assume art is started by dalvikvm, i.e., from the command line,
      // not by zygote.

      snprintf(buf, size, "/proc/%d/cmdline", pid);
      FILE *fp = fopen(buf, "r");
      size_t count = 0;
      if (fp) {
        char tmp[1024];
        count = fread(tmp, sizeof(char), size, fp);
        if (count) {
          size_t i = 0;
          while (i < count) {
            if (!strcmp(tmp + i, "-cp")) {
              i += 4;
              break;
            }
            while (tmp[i] != '\0' && i < count)
              ++i;
            ++i;
          }
          DCHECK_LT(i, count);
          strcpy(buf, tmp + i);

          // remove suffix name
          char *suffix = strstr(buf, ".jar");
          if (!suffix)
            suffix = strstr(buf, ".dex");
          if (suffix)
            *suffix = '\0';

          ALOGD("%s: %s\n", __FUNCTION__, buf);
        }
        fclose(fp);
      }

      if (!count)
        snprintf(buf, size, "%d", pid);
    }


    int LeakTracer::Create(const char *proc_name) {
      char *name = const_cast<char*>(proc_name);
      bool track = false;

      if (NULL == proc_name) {
        const int kBufSize = 1024;
        name = new char[kBufSize];
        GetProcNameByPid(name, kBufSize, getpid());

        // if we are running from the command line,
        // try to get the benchmark name.
        if (!strcmp(name, "dalvikvm")) {
        GetBenchmarkName(name, kBufSize, getpid());
        track = true;
        }
      }

      // Log which procedure is being tracked.
      ALOGD("Meeting :  %s\n", name);

      if (!track) {
        InitBenchmarkSet();
        track = gBenchmarks.end() != gBenchmarks.find(name);
      }

      if (track) {
        InitNoTrackSet();
        track = noTrackSet.end() == noTrackSet.find(name);
      }

      int errcode = -1;
      if (track) {
        instance_ = new LeakTracer(name);

        errcode = instance_->OpenDataFile();
        if (errcode)
          gLogOnly = true;

        errcode = instance_->OpenClassFile();
        if (errcode)
          gLogOnly = true;

        // if(false == gLogOnly) {
        //   if (signal(SIGUSR1, exitSigHandler) != SIG_ERR)
        //     // Log quit information.
        //     ALOGD("Register Quit sig hander!\n");
        // }

        gLeakTracerIsTracking = true;
        Instance()->AppStarted();
      } else {
        return 42;  // 42 represent that we don't Track this APP
      }

      if (NULL == proc_name)
        delete [] name;

      return errcode;
    }


    void LeakTracer::NewObject(void *addr, size_t size) {
      if (gLeakTracerIsTracking && !gLogOnly) {
        // mirror::Object* obj = reinterpret_cast<mirror::Object*>(addr);
        // const uint32_t monitor = obj->GetM();
        // ReaderMutexLock mu(Thread::Current(), *Locks::mutator_lock_);
        // switch (obj->GetLockWord(false).GetState())

        uint32_t klass = *reinterpret_cast<uint32_t*>(addr);
        Thread *self = Thread::Current();
        size_t array_size = self->GetArrayAllocSize();
        bool is_large_object = self->GetIsLargeObj();
        self->SetIsLargeObj(false);
        ObjectKind obj_kind = kNormalObject;
        //
        // the order is:
        // obj_addr, alloc_site, klass [, size]
        //
        // the least two bits of klass indicates what kind of object is, and
        // if large or array object, their size will follow klass immediately.
        //
        if (is_large_object) {
          obj_kind = kLargeObject;
          // ALOGD("Meeting Large Object: %p   Size:%d\n", addr, static_cast<int>(size));
        } else if (array_size) {
          DCHECK_EQ(size, array_size);
          self->SetArrayAllocSize(0U);
          obj_kind = kArrayObject;
          // ALOGD("Meeting Array Object: %p  Size: %d\n", addr, (int)size);
        }

        u32 data[4], count = 0;
        data[count++] = reinterpret_cast<uintptr_t>(addr);
        data[count++] = reinterpret_cast<uint32_t>(self->GetAllocSite());
        if (obj_kind != kNormalObject) {
          data[count++] = reinterpret_cast<uint32_t>(klass) | static_cast<uint32_t>(obj_kind);
          data[count++] = size;
        } else {
          data[count++] = reinterpret_cast<uint32_t>(klass);
        }
        // data[count++] = monitor;
        WriteSafe(data, count * sizeof(data[0]));
        NewClass(reinterpret_cast<mirror::Class*>(klass));
      }
    }




    void LeakTracer::DeadObject(void *addr) {
      if (gLeakTracerIsTracking) {
        if (gLogOnly) {
          // ALOGD("DEAD: %p\n", addr);
        } else {
          u32 data = reinterpret_cast<uintptr_t>(addr) | kReclaimObject;
          WriteSafe(&data, sizeof data);
        }
      }
    }




    void LeakTracer::AccessObject(void *addr) {
      if (gLeakTracerIsTracking) {
        if (gLogOnly) {
          // ALOGD("ACCESS: %p\n", addr);
        } else {
          u32 data = reinterpret_cast<uintptr_t>(addr) | kAccessObject;
	  acc_sets.insert(data);
	  // if (acc_objs.size() % 400 == 0) {
	  //   int x = acc_objs.size();
	  //   ALOGD("Accessed Objs %d   %d\n", x, acc_count);
	  // }
          // u32 data = reinterpret_cast<uintptr_t>(addr) | kAccessObject;
          // WriteSafe(&data, sizeof data);
	  // mirror::Object* obj = reinterpret_cast<mirror::Object*>(addr);
          // uint32_t monitor = obj->GetM();
	  // WriteSafe(&monitor, sizeof monitor);
        } 
      }
    }




    void LeakTracer::MoveObject(void *from, void *to) {
      if (gLeakTracerIsTracking) {
        if (gLogOnly) {
          // ALOGD("MOVE: %p -> %p\n", from, to);
        } else {
          u32 data[] = {
            static_cast<u32>(reinterpret_cast<uintptr_t>(from)) | kMoveObject,
            static_cast<u32>(reinterpret_cast<uintptr_t>(to))
          };
          WriteSafe(data, sizeof data);
        }
      }
    }




    void LeakTracer::NewClass(mirror::Class *klass) {
      if (klass && classes_.find(klass) == classes_.end()) {
        classes_.insert(klass);

        ReaderMutexLock mu(Thread::Current(), *Locks::mutator_lock_);
        std::string class_name = PrettyDescriptor(klass);
        uint32_t obj_size = klass->GetObjectSize();
        // if (obj_size == 0U)
        //   obj_size = klass->GetClassSize();

        // leading character 'c' means this line is Class information
        // NOTE: object size may be zero if class is interface or abstract
        // PRINT(class_fp_, "c %8X %8u %s\n",
        //       reinterpret_cast<uint32_t>(klass),
        //       obj_size,
        //       class_name.c_str());

        class_meta_info_.push_back(ClassOrMethod(
                                     reinterpret_cast<uintptr_t>(klass),
                                     obj_size,
                                     class_name));

        // auto DumpMethods = [&](mirror::ObjectArray<ArtMethod>* array) {
        //   if (array) {
        //     int len = array->GetLength();
        //     for (int i = 0; i < len; ++i)
        //       NewMethodLinked(class_name, *(array->Get(i)));
        //   }
        // };
        size_t pointer_size = Runtime::Current()->GetClassLinker()->GetImagePointerSize();
        auto direct_methods = klass->GetDirectMethods(pointer_size);
        for (auto& m : direct_methods) {
          NewMethodLinked(class_name, m);
        }
        auto virtual_methods = klass->GetVirtualMethods(pointer_size);
        for (auto& m : virtual_methods) {
          NewMethodLinked(class_name, m);
        }
        for (auto i = 0; i < klass->GetVTableLength(); i++) {
          NewMethodLinked(class_name, *(klass->GetVTableEntry(i, pointer_size)));
        }
        // auto im_methods = klass->GetImTable();
        // if (im_methods)
        //   for (int i = 0; i < im_methods->GetLength(); ++i)
        //     NewMethodLinked(class_name, im_methods->Get(i));
        // DumpMethods(im_methods);

        // interface table is an array of pair (Class*, ObjectArray<ArtMethod*>)
        auto if_table = klass->GetIfTable();
        size_t method_array_size = klass->GetIfTableCount();
        for (size_t i = 0; i < method_array_size; ++i) {
          auto* method_array = if_table->GetMethodArray(i);
          for (size_t k = 0; k < if_table->GetMethodArrayCount(i); k++) {
            NewMethodLinked(class_name, *(method_array->GetElementPtrSize<ArtMethod*>(k, pointer_size)));
          }
        }
      }
    }




    void LeakTracer::NewMethodLinked(const std::string& class_name, ArtMethod &method) {
        // leading character 'm' means this line is Method information
        uint32_t code = reinterpret_cast<uintptr_t>(method.GetEntryPointFromQuickCompiledCode());
        ReaderMutexLock mu(Thread::Current(), *Locks::mutator_lock_);
        std::string method_name(class_name + "::" + method.GetName());
        // PRINT(class_fp_, "m %8X %8X %s\n", code, code + method->GetCodeSize(), method_name.c_str());
        method_meta_info_.push_back(ClassOrMethod(reinterpret_cast<unsigned int>(code),
                                                  reinterpret_cast<unsigned int>(code) + method.GetCodeSize(),
                                                  method_name));
    }



    void LeakTracer::GcStarted(bool is_compacting_gc) {
      ++num_gc_;
      if (gLeakTracerIsTracking) {
        if (gLogOnly) {
          ALOGD("%s %zd\n", __FUNCTION__, num_gc_);
        } else {
          uint32_t data[] = {kGcStart, is_compacting_gc};
          WriteSafe(data, sizeof data);
        }
      }
    }



    void LeakTracer::GcFinished() {
      if (gLeakTracerIsTracking) {
        if (gLogOnly) {
          ALOGD("%s\n", __FUNCTION__);
        } else {
	  int n =  acc_sets.size();
	  for (auto x : acc_sets) {
	    WriteSafe(&x, sizeof x);
	  }
	  ALOGD("Accessed Objs %d\n", n);
	  ALOGD("Bucket   Count %d\n", (int)acc_sets.bucket_count());
	  ALOGD("Load     Factor %f\n", acc_sets.load_factor());  
	  acc_sets.clear();
          uint32_t raw = kGcEnd;
          WriteSafe(&raw, sizeof raw);
        }
      }
    }




    void LeakTracer::AppStarted() {
      ALOGD("%s\n", __FUNCTION__);

      if (gLeakTracerIsTracking && !gLogOnly) {
        uint32_t raw = kAppStart;
        WriteSafe(&raw, sizeof raw);
      }
    }




    void LeakTracer::AppFinished() {
      ALOGD("%s\n", __FUNCTION__);

      if (gLeakTracerIsTracking && !gLogOnly) {
        gLeakTracerIsTracking = false;

        DumpTypeAndMethodInfo();

        for (auto &klass : class_meta_info_)
          PRINT(class_fp_, "c %8X %8u %s\n", klass.data1, klass.data2, klass.name.c_str());
        for (auto &method : method_meta_info_)
          PRINT(class_fp_, "m %8X %8X %s\n", method.data1, method.data2, method.name.c_str());

        uint32_t raw = kAppEnd;
        WriteSafe(&raw, sizeof raw);
        CloseFile(data_fp_);
        CloseFile(class_fp_);
      }
    }




    int LeakTracer::OpenDataFile() {
      char path[1024], benchmark_name[256];

      if (proc_name_ == "dalvikvm") {
        GetProcNameByPid(benchmark_name, sizeof benchmark_name, getpid());
        proc_name_ = benchmark_name;
      }

      sprintf(path, "/data/local/tmp/%s.data", proc_name_.c_str());
      data_fp_ = fopen(path, "wb");
      if (data_fp_ == nullptr)
        ALOGD("Creating %s failed, because %s\n", path, strerror(errno));
      return data_fp_ == nullptr ? errno : 0;
    }




    int LeakTracer::OpenClassFile() {
      char path[1024];

      sprintf(path, "/data/local/tmp/%s.txt", proc_name_.c_str());
      class_fp_ = fopen(path, "w");
      if (class_fp_ == nullptr)
        ALOGD("Creating %s failed, because %s\n", path, strerror(errno));
      return class_fp_ == nullptr ? errno : 0;
    }



    void LeakTracer::CloseFile(FILE *& fp) {
      if (fp) {
        fflush(fp);
        fclose(fp);
        fp = nullptr;
      }
    }




    void LeakTracer::DumpTypeAndMethodInfo() {
      auto& table = Runtime::Current()->GetClassLinker()->GetClassTable();
      for (auto& it : table) {
        ReaderMutexLock mu(Thread::Current(), *Locks::mutator_lock_);
         mirror::Class *klass = it.Read<kWithoutReadBarrier>();

        // thought we have found a new class!
        NewClass(klass);

        // std::vector<mirror::ObjectArray<mirror::ArtMethod>*> methods;
        // methods.push_back(klass->GetDirectMethods());
        // methods.push_back(klass->GetVirtualMethods());
        // methods.push_back(klass->GetImTable());
        // for (const auto method_array : methods)
        //   if (method_array)
        //     for (int i = 0; i < method_array->GetLength(); ++i)
        //       NewMethodLinked(method_array->Get(i));
      }
    }




    LeakTracer::LeakTracer(const char *proc_name)
      :data_fp_(nullptr),
      class_fp_(nullptr),
      proc_name_(proc_name),
      num_gc_(0U),
      acc_sets(9000) {
        // gc_thread_pool_ = Runtime::Current()->GetHeap()->GetThreadPool();
        ALOGD("MaxSize %d\n", (int)acc_sets.max_size());
        ALOGD("MaxBucketCount %d\n", (int)acc_sets.bucket_count());
        ALOGD("MaxSize %f\n", acc_sets.load_factor());      
        ALOGD("%s\n", __FUNCTION__);
    }




    LeakTracer::~LeakTracer() {
      ALOGD("%s\n", __FUNCTION__);
      CloseFile(data_fp_);
      CloseFile(class_fp_);
      gLeakTracerIsTracking = false;
      instance_ = nullptr;
    }
}  // namespace leaktracer
}  // namespace art

