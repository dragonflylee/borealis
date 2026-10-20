#!/usr/bin/env python3
"""Actual native scheduler ownership, failures, ordering and cancellation."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
BRLS = ROOT / 'library'
BODY = r'''
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#include <list>
#include <set>
#include <string>
#include <cassert>
#include <cstring>
#include <stdexcept>
#include <cstdio>
#include <cstdlib>
#include <new>
static std::atomic<bool> denyAllocations{false};
void* operator new(std::size_t n){if(denyAllocations.load())throw std::bad_alloc();if(void*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}
void operator delete(void*p,std::size_t)noexcept{std::free(p);}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete[](void*p)noexcept{std::free(p);}
void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#define private public
#include <borealis/core/thread.hpp>
#undef private
#include "@SOURCE@"
static bool oneBatch=false,throwCopies=false,throwLogging=false;
static int calls[32]{},used=0,copies=0,logs=0;
static void mark(int value){assert(used<32);calls[used++]=value;}
void retro_sleep(unsigned){if(oneBatch)brls::nativeTaskLoopActive=false;else std::this_thread::yield();}
void observed_log(){++logs;if(throwLogging)throw std::bad_alloc();}
struct CopyBomb {
 int value;
 explicit CopyBomb(int n):value(n){}
 CopyBomb(const CopyBomb& other):value(other.value){++copies;if(throwCopies)throw std::runtime_error("copy failure");}
 void operator()()const{mark(value);}
};
static bool available(std::mutex& mutex){
 bool result=false;std::thread thread([&]{result=mutex.try_lock();if(result)mutex.unlock();});thread.join();return result;
}
static int copy_fault(const std::string& mode){
 using T=brls::Threading;
 std::function<void()> task=CopyBomb(1);
 if(mode=="copy-sync")T::sync(task);
 else if(mode=="copy-async")T::async(task);
 else T::delay(mode=="copy-future"?60000:0,task);
 copies=0;throwCopies=true;oneBatch=true;bool threw=false;
 try{if(mode=="copy-async")T::task_loop(nullptr);else T::performSyncTasks();}catch(...){threw=true;}
 throwCopies=false;
 bool unlocked=available(T::m_sync_mutex)&&available(T::m_async_mutex)&&available(T::m_delay_mutex);
 // The pre-fix fixture must exit without leaving a mutex owned at destruction.
 if(!available(T::m_sync_mutex))T::m_sync_mutex.unlock();
 if(!available(T::m_delay_mutex))T::m_delay_mutex.unlock();
 if(threw||!unlocked||copies){std::fprintf(stderr,"FAIL drain threw=%d mutexes_available=%d copies=%d\n",threw,unlocked,copies);return 2;}
 if(mode=="copy-future"){
  assert(!used&&brls::nativeDelayTasks.size()==1);
  T::cancelDelay(brls::nativeDelayTasks.front().index);T::performSyncTasks();assert(brls::nativeDelayTasks.empty());
 }else assert(used==1&&calls[0]==1);
 return 0;
}
static void ordering(){
 using T=brls::Threading;
 T::sync([]{mark(1);T::sync([]{mark(5);});T::delay(0,[]{mark(3);});});
 T::sync([]{mark(2);});
 T::performSyncTasks();assert(used==3&&calls[0]==1&&calls[1]==2&&calls[2]==3);
 T::performSyncTasks();assert(used==4&&calls[3]==5);
 // A timer created during the timer batch is deferred; an original future
 // timer remains queued without copying or reallocation of its callback.
 used=0;auto future=T::delay(60000,CopyBomb(9));
 size_t cancelled=0;
 T::delay(0,[&]{mark(1);T::cancelDelay(cancelled);T::delay(0,[]{mark(4);});T::sync([]{mark(3);});});
 cancelled=T::delay(0,[]{mark(99);});T::delay(0,[]{mark(2);});
 copies=0;throwCopies=true;T::performSyncTasks();throwCopies=false;
 assert(!copies&&used==2&&calls[0]==1&&calls[1]==2&&brls::nativeDelayTasks.size()==2);
 T::performSyncTasks();assert(used==4&&calls[2]==3&&calls[3]==4);
 T::cancelDelay(future);T::performSyncTasks();assert(brls::nativeDelayTasks.empty()&&T::m_delay_cancel_set.empty());
 // Erasing a completed timer's own cancellation retains the existing API.
 size_t self=0;self=T::delay(0,[&]{T::cancelDelay(self);});T::performSyncTasks();assert(T::m_delay_cancel_set.empty());
}
static void exceptions(){
 using T=brls::Threading;
 T::sync([]{throw std::runtime_error("fixture");});T::sync([]{throw 7;});T::sync([]{mark(1);});
 T::delay(0,[]{throw 8;});T::delay(0,[]{mark(2);});
 throwLogging=true;T::performSyncTasks();throwLogging=false;
 assert(used==2&&calls[0]==1&&calls[1]==2&&brls::nativeDelayTasks.empty());
 T::async([]{throw std::runtime_error("fixture");});T::async([]{throw 9;});T::async([]{mark(3);});
 oneBatch=true;throwLogging=true;T::task_loop(nullptr);throwLogging=false;
 assert(used==3&&calls[2]==3&&available(T::m_async_mutex));
}
static void concurrency(){
 using T=brls::Threading;
 std::atomic<int> done{0},syncDone{0};
 T::start();
 std::vector<std::thread> senders;
 for(int p=0;p<4;++p)senders.emplace_back([&]{for(int i=0;i<100;++i)T::async([&]{done.fetch_add(1);T::sync([&]{syncDone.fetch_add(1);});});});
 for(auto& t:senders)t.join();
 auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
 while(syncDone.load()!=400&&std::chrono::steady_clock::now()<deadline){T::performSyncTasks();std::this_thread::yield();}
 assert(done==400&&syncDone==400);T::stop();
 assert(T::m_async_tasks.empty()&&T::m_sync_functions.empty());
}
static void allocation_faults(){
 using T=brls::Threading;
 for(int kind=0;kind<3;++kind){
  bool failed=false;denyAllocations=true;
  try{if(kind==0)T::sync([]{});else if(kind==1)T::async([]{});else T::delay(0,[]{});}catch(const std::bad_alloc&){failed=true;}
  denyAllocations=false;assert(failed);
  assert(available(T::m_sync_mutex)&&available(T::m_async_mutex)&&available(T::m_delay_mutex));
  assert(T::m_sync_functions.empty()&&T::m_async_tasks.empty()&&brls::nativeDelayTasks.empty());
 }
 T::sync(CopyBomb(1));T::delay(0,CopyBomb(2));auto future=T::delay(60000,CopyBomb(4));T::async(CopyBomb(3));
 copies=0;throwCopies=true;denyAllocations=true;oneBatch=true;
 T::performSyncTasks();T::task_loop(nullptr);
 denyAllocations=false;throwCopies=false;
 assert(!copies&&used==3&&calls[0]==1&&calls[1]==2&&calls[2]==3&&brls::nativeDelayTasks.size()==1);
 T::cancelDelay(future);denyAllocations=true;T::performSyncTasks();denyAllocations=false;
 assert(brls::nativeDelayTasks.empty()&&T::m_delay_cancel_set.empty());
}
static void logging_limit(){
 using T=brls::Threading;
 for(int i=0;i<100;++i)T::sync([]{throw 1;});
 T::sync([]{mark(1);});T::performSyncTasks();
 assert(logs==16&&used==1&&calls[0]==1);
 for(int i=0;i<100;++i)T::async([]{throw 2;});
 oneBatch=true;T::task_loop(nullptr);assert(logs==16);
}
int main(int argc,char**argv){
 assert(argc==2);std::string mode=argv[1];
 if(mode.find("copy-")==0)return copy_fault(mode);
 if(mode=="ordering")ordering();else if(mode=="exceptions")exceptions();else if(mode=="concurrency")concurrency();
 else if(mode=="allocation")allocation_faults();else if(mode=="logging-limit")logging_limit();else assert(false);
}
'''


class NativeThreadingTests(unittest.TestCase):
    def test_actual_scheduler(self):
        with tempfile.TemporaryDirectory(prefix='native-scheduler-') as temp:
            work = Path(temp)
            (work / 'libretro-common').mkdir()
            (work / 'libretro-common/retro_timers.h').write_text('#pragma once\nvoid retro_sleep(unsigned);\n')
            (work / 'borealis/core').mkdir(parents=True)
            (work / 'borealis/core/logger.hpp').write_text('''#pragma once
void observed_log();
namespace brls {struct Logger {template<class... A>static void error(const char*,A&&...){observed_log();}};}
''')
            cpp = work / 'test.cpp'
            cpp.write_text(BODY.replace('@SOURCE@', str(BRLS / 'lib/core/thread.cpp')))
            env = dict(os.environ, ASAN_OPTIONS='detect_leaks=' + ('0' if sys.platform == 'darwin' else '1'),
                       UBSAN_OPTIONS='halt_on_error=1')
            modes = os.environ.get('NATIVE_SCHEDULER_CASES', 'copy-sync copy-async copy-delay copy-future ordering exceptions concurrency allocation logging-limit').split()
            for threads in ('std', 'pthread'):
                binary = work / ('test-' + threads)
                subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17', '-O2', '-g',
                                '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                                '-fsanitize=' + os.environ.get('NATIVE_SCHEDULER_SANITIZERS', 'address,undefined'), '-fno-omit-frame-pointer',
                                '-DPS5_NATIVE_GPU', *(['-DBOREALIS_USE_STD_THREAD'] if threads == 'std' else []),
                                '-I', str(work), '-I', str(BRLS / 'include'), str(cpp), '-pthread', '-o', str(binary)], check=True)
                for mode in modes:
                    with self.subTest(threads=threads, mode=mode):
                        subprocess.run([str(binary), mode], check=True, env=env, timeout=15)


if __name__ == '__main__': unittest.main()
