// This file is part of the HörTech Open Master Hearing Aid (openMHA)
// Copyright © 2006 2008 2009 2010 2011 2013 2016 2017 2018 2020 HörTech gGmbH
//
// openMHA is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published by
// the Free Software Foundation, version 3 of the License.
//
// openMHA is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License, version 3 for more details.
//
// You should have received a copy of the GNU Affero General Public License, 
// version 3 along with openMHA.  If not, see <http://www.gnu.org/licenses/>.

#ifndef __MHA_FIFO_H__
#define __MHA_FIFO_H__

#include <algorithm>
#include <vector>
#include <atomic>
#include "mha_error.hh"

/** 
 * A FIFO class.
 * Synchronization: None.  Use external synchronisation or synchronization 
 * in inheriting class.  Assignment, copy and move constructors are disabled.
 */
template <class T>
class mha_fifo_t {
    /** The memory allocated to store the data in the fifo.
     * At least one location in buf is always unused, because we have
     * max_fill_count + 1 possible fillcounts [0:max_fill_count] that we 
     * need to distinguish. */
    std::vector<T> buf;

    /** points to location where to write next */
    T * write_ptr;

    /**  points to location where to read next */
    const T * read_ptr;
public:
    /** The data type exchanged by this fifo */
    typedef typename std::vector<T>::value_type value_type;

    /** write specified ammount of data to the fifo.
     * @param data Pointer to source data.
     * @param count Number of instances to copy
     * @throw MHA_Error when there is not enough space available. */
    virtual void write(const T * data, unsigned count);

    /** read data from fifo
     * @param outbuf Pointer to the target buffer.
     * @param count  Number of instances to copy.
     * @throw MHA_Error when there is not enough data available. */
    virtual void read(T * outbuf, unsigned count);

    /** Read-only access to fill_count */
    virtual unsigned get_fill_count() const {
        return get_fill_count(write_ptr, read_ptr);
    }

    /** Read-only access to available_space */
    virtual unsigned get_available_space() const;

    /** The capacity of this fifo */
    virtual unsigned get_max_fill_count() const {return buf.size()-1U;}

    /** Create FIFO with fixed buffer size, where all (initially unused) 
        instances of T are initialized as copies of t.
        @param max_fill_count The maximum number of instances of T that can
               be held at the same time inside the fifo.
        @param The fifo allocates a vector of max_fill_count+1 instances of
               T for storage, one of which is always unused. */
    explicit mha_fifo_t(unsigned max_fill_count, const T & t = T());

    /** Make destructor virtual. */
    virtual ~mha_fifo_t() = default;

    /** Copy constructor */
    mha_fifo_t(const mha_fifo_t&) = delete;

    /** Move constructor */
    mha_fifo_t(mha_fifo_t&&) = delete;

    /** Assignment operator */
    mha_fifo_t<T>& operator=(const mha_fifo_t<T>&) = delete;

    /** Move assignment operator */
    mha_fifo_t<T>& operator=(mha_fifo_t<T>&&) = delete;

protected:
    /** Empty the fifo at once. Should be called by the reader, or
     * when the reader is inactive. */
    void clear() {read_ptr = write_ptr;}

    /** read-only access to the write pointer for derived classes */
    const T * get_write_ptr() const {return write_ptr;}

    /** read-only access to read pointer for derived classes */
    const T * get_read_ptr() const {return read_ptr;}

    /** Compute fill count from given write pointer and read pointer.
     * @param wp Write pointer.
     * @param rp Read pointer.
     * @pre wp and rp must point to an instance of T inside buf.
     * @return Number of elements that can be read from the fifo when wp and rp
     *         have the given values. */
    inline unsigned get_fill_count(const T * wp, const T * rp) const {
        if (wp >= rp) // Direct subtraction will not underflow in this case.
            // Direct difference will also not overflow when converted to return
            // type unsigned, see overflow check in mha_fifo_t<T> constructor.
            return wp - rp;
        // Avoid underflow (buf is used as a ringbuffer).
        return wp + buf.size() - rp;
    }
};

/**
 * A lock-free FIFO class for transferring data from a producer thread to
 * a consumer thread.  Inherits basic functionality from mha_fifo_t, adds
 * release-acquire semantics to ensure consumer that the fill count or free
 * space deduced from read and write pointers is consistent with the actual
 * data.  Copying, moving, and assignment of FIFO are forbidden by base class.
 */
template <class T>
class mha_fifo_lf_t : public mha_fifo_t<T> {
    /** atomic copy of the write_ptr, only modified by the producer thread */
    std::atomic<const T *> atomic_write_ptr;

    /** atomic copy of the read ptr, only modified by the consumer thread */
    std::atomic<const T *> atomic_read_ptr;
public:
    /** Write specified ammount of data to the fifo.
     * Must only be called by the writer thread.
     * @param data  Pointer to source data.
     * @param count Number of instances to copy.
     * @throw MHA_Error when there is not enough space available. */
    virtual void write(const T * data, unsigned count) override {
        mha_fifo_t<T>::write(data, count);
        // The following store-release is used by a corresponding load-acquire
        // in method get_fill_count
        atomic_write_ptr.store(this->get_write_ptr());
    }
    /** Read data from fifo. Must only be called by the reader thread.
     * @param outbuf Pointer to the target buffer.
     * @param count  Number of instances to copy.
     * @throw MHA_Error when there is not enough data available. */
    virtual void read(T * outbuf, unsigned count) override {
        mha_fifo_t<T>::read(outbuf, count);
        // The following store-release is used by a corresponding load-acquire
        // in method get_available_space
        atomic_read_ptr.store(this->get_read_ptr());
    }
    /** get_fill_count() must only be called by the reader thread */
    virtual unsigned get_fill_count() const override {
        // we need to load-acquire the atomic write pointer, but we can
        // read the read pointer non-atomically, because it is only modified
        // by the reader thread, in which we execute.
        return mha_fifo_t<T>::get_fill_count(atomic_write_ptr.load(),
                                             this->get_read_ptr());
    }
    /** get_available_space() must only be called by the writer thread */
    virtual unsigned get_available_space() const override {
        // we can read the write pointer non-atomically, because it is only
        // modified by the writer thread, but we need to load-aquire the atomic
        // read pointer.
        return this->get_max_fill_count()
            - mha_fifo_t<T>::get_fill_count(this->get_write_ptr(),
                                         atomic_read_ptr.load());
    }
    /** Create FIFO with fixed buffer size. All (initially unused)
        instances of T are initialized as copies of t */
    explicit mha_fifo_lf_t(unsigned max_fill_count, const T & t = T())
        : mha_fifo_t<T>(max_fill_count, t),
          atomic_write_ptr(this->get_write_ptr()),
          atomic_read_ptr(this->get_read_ptr()) {
    }
};


// forward reference for the friend test class.
class Test_mha_drifter_fifo_t;

/** 
 * A FIFO class for blocksize adaptation without Synchronization.
 * Features: 
 * delay concept (desired, minimum and maximum delay),
 * drifting support by throwing away data or inserting zeroes.
 */
template <class T>
class mha_drifter_fifo_t : public mha_fifo_t<T> {
    /** Tester class gets access to private variables. */
    friend class Test_mha_drifter_fifo_t;

    /** The minimum fill count of this fifo. */
    const unsigned minimum_fill_count;

    /** The desired fill count of the fifo. The fifo is initialized with this
     * ammount of data when data transmission starts. */
    const unsigned desired_fill_count;

    /** Flag set to true when write is called the first time. */
    bool writer_started;

    /** Flag set to true when read is called for the first time. */
    bool reader_started;

    /** The number of xruns seen by the writer since object instantiation */
    unsigned writer_xruns_total;

    /** The number of xruns seen by the reader since object instantiation */
    unsigned reader_xruns_total;

    /** The number of xruns seen by the writer since the last start of
     * processing. */
    unsigned writer_xruns_since_start;

    /** The number of xruns seen by the reader since the last start of
     * processing */
    unsigned reader_xruns_since_start;

    /** The number of xruns seen by the writer in succession. 
     * Reset to 0 every time a write succeeds without xrun. */
    unsigned writer_xruns_in_succession;

    /** The number of xruns seen by the reader in succession.
     * Reset to 0 every time a read succeeds without xrun. */
    unsigned reader_xruns_in_succession;

    /** A limit to the number of xruns seen in succession during write
     * before the data transmission through the FIFO is stopped. */
    unsigned maximum_writer_xruns_in_succession_before_stop;

    /** A limit to the number of xruns seen in succession during read
     * before the data transmission through the FIFO is stopped. */
    unsigned maximum_reader_xruns_in_succession_before_stop;

    /** The value used in place of missing data */
    typename mha_fifo_t<T>::value_type null_data;

    /** When processing starts, that is when both
     * mha_drifter_fifo_t<T>::reader_started and
     * mha_drifter_fifo_t<T>::writer_started are true, then first
     * mha_drifter_fifo_t<T>::desired_fill_count instances of 
     * mha_drifter_fifo_t<T>::null_data are delivered to the reader.  These
     * #null_data instances are not transmitted through the fifo
     * because filling the fifo with enough #null_data might not be
     * realtime safe and this filling has to be initiated by #starting
     * or #stop (this implementation: #starting) which are be called
     * with realtime constraints. */
    unsigned startup_zeros;

public:
    /** \brief write data to fifo
     *
     * Sets #writer_started to true.
     *
     * When processing has started, i.e. both #reader_started and
     * #writer_started are true, write specified ammount of data to
     * the fifo.  If there is not enough space available, then the
     * exceeding data is lost and the writer xrun counters are
     * increased.
     *
     * Processing is stopped when #writer_xruns_in_succession exceeds
     * #maximum_writer_xruns_in_succession_before_stop.
     *
     * @param data Pointer to source data.
     * @param count Number of instances to copy */
    virtual void write(const T * data, unsigned count);

    /** \brief Read data from fifo.
     *
     * Sets #reader_started to true.
     * 
     * When processing has started, i.e. both #reader_started and
     * #writer_started are true, then read specified ammount of data
     * from the fifo.  As long as #startup_zeros is > 0, #null_data is
     * delivered to the reader and #startup_zeros is diminished. Only
     * when #startup_zeros has reached 0, data is actually read from
     * the fifo's buffer.
     *
     * If the read would cause the fifo's fill
     * count to drop below #minimum_fill_count, then only so much data
     * are read that #minimum_fill_count entries remain in the fifo,
     * the missing data is replaced with #null_data, and the reader
     * xrun counters are increased.
     *
     * Processing is stopped when #reader_xruns_in_succession exceeds
     * #maximum_reader_xruns_in_succession_before_stop.
     *
     * @param buf Pointer to the target buffer
     * @param count Number of instances to copy */
    virtual void read(T * buf, unsigned count);

    /** Return fill_count, adding mha_drifter_fifo_t<T>::startup_zeros to the
     * number of samples actually in the fifo's buffer. */
    virtual unsigned get_fill_count() const;

    /** Return available space, subtracting number of
     * mha_drifter_fifo_t<T>::startup_zeros from 
     * the available_space actually present in the fifo's buffer. 
     * TODO: uncertain if this is a good idea. */
    virtual unsigned get_available_space() const;

    /** The desired fill count of this fifo */
    virtual unsigned get_des_fill_count() const {return desired_fill_count;}

    /** The minimum fill count of this fifo */
    virtual unsigned get_min_fill_count() const {return minimum_fill_count;}

    /** Called by mha_drifter_fifo_t<T>::read or mha_drifter_fifo_t<T>::write
     * when their xrun in succession counter exceeds its limit. 
     * May also be called explicitly. */
    virtual void stop();

    /** Called by mha_drifter_fifo_t<T>::read or mha_drifter_fifo_t<T>::write
     * when the respective flag (mha_drifter_fifo_t<T>::reader_started or 
     * mha_drifter_fifo_t<T>::writer_started) is about to be toggled
     * from false to true.  The fifo's buffer is emptied, this method
     * resets #startup_zeros to #desired_fill_count, and it also
     * resets #reader_xruns_since_start and #writer_xruns_since_start
     * to 0. */
    virtual void starting();

    /** Create drifter FIFO. */
    mha_drifter_fifo_t(unsigned min_fill_count,
                       unsigned desired_fill_count,
                       unsigned max_fill_count);

    /** Create drifter FIFO where all (initially unused) 
     * copies of T are initialized as copies of t */
    mha_drifter_fifo_t(unsigned min_fill_count,
                       unsigned desired_fill_count,
                       unsigned max_fill_count,
                       const T & t);
};


/** Abstract base class for synchronizing multithreaded (producer/consumer) 
    fifo operations. Works only with single producer and single consumer. */
class mha_fifo_thread_platform_t {
public:
    /** Calling thread waits until it aquires the lock.
        Must not be called when the lock is already aquired. */
    virtual void aquire_mutex() = 0;
    /** Calling thread releases the lock.
        May only be called when lock is owned. */
    virtual void release_mutex() = 0;
    /** Calling producer thread must own the lock. Method releases lock, and
        waits for consumer thread to call decrease(). Then reaquires lock and
        returns */
    virtual void wait_for_decrease() = 0;
    /** Calling consumer thread must own the lock. Method releases lock, and
        waits for producer thread to call increase(). Then reaquires lock and
        returns */
    virtual void wait_for_increase() = 0;
    /** To be called by producer thread after producing. Producer thread needs
        to own the lock to call this method. */
    virtual void increment() = 0;
    /** To be called by consumer thread after consuming. Consumer thread needs
        to own the lock to call this method. */
    virtual void decrement() = 0;
    /** Make destructor virtual */
    virtual ~mha_fifo_thread_platform_t() {}
private:
    // disallow copy constructor and assignment operator
    mha_fifo_thread_platform_t(const mha_fifo_thread_platform_t &);
    mha_fifo_thread_platform_t & operator=(const mha_fifo_thread_platform_t &);
public:
    /** Make default constructor accessible */
    mha_fifo_thread_platform_t() {}
};

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
class mha_fifo_win32_threads_t : public mha_fifo_thread_platform_t {
    CRITICAL_SECTION mutex;
    HANDLE decrease_event;
    HANDLE increase_event;
public:
    mha_fifo_win32_threads_t() {
        InitializeCriticalSection(&mutex);
        decrease_event = CreateEvent(NULL, FALSE, FALSE, NULL);
        increase_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    }
    virtual void aquire_mutex() {
        EnterCriticalSection(&mutex);
    }
    virtual void release_mutex() {
        LeaveCriticalSection(&mutex);
    }
    virtual void wait_for_decrease() {
        release_mutex();
        WaitForSingleObject(decrease_event, INFINITE);
        aquire_mutex();
    }
    virtual void wait_for_increase() {
        release_mutex();
        WaitForSingleObject(increase_event, INFINITE);
        aquire_mutex();
    }
    virtual void increment() {
        SetEvent(increase_event);
    }
    virtual void decrement() {
        SetEvent(decrease_event);
    }
    virtual ~mha_fifo_win32_threads_t() {
        CloseHandle(increase_event);
        CloseHandle(decrease_event);
        DeleteCriticalSection(&mutex);
    }
};
#define mha_fifo_thread_platform_implementation_t mha_fifo_win32_threads_t

#else // _WIN32
#include <pthread.h>

class mha_fifo_posix_threads_t : public mha_fifo_thread_platform_t {
    pthread_mutex_t mutex;
    pthread_cond_t decrease_condition;
    pthread_cond_t increase_condition;
public:
    mha_fifo_posix_threads_t() {
        pthread_mutex_init(&mutex, 0);
        pthread_cond_init(&decrease_condition, 0);
        pthread_cond_init(&increase_condition, 0);
    }
    virtual void aquire_mutex() {
        pthread_mutex_lock(&mutex);
    }
    virtual void release_mutex() {
        pthread_mutex_unlock(&mutex);
    }
    virtual void wait_for_decrease() {
        pthread_cond_wait(&decrease_condition, &mutex);
    }
    virtual void wait_for_increase() {
        pthread_cond_wait(&increase_condition, &mutex);
    }
    virtual void increment() {
        pthread_cond_signal(&increase_condition);
    }
    virtual void decrement() {
        pthread_cond_signal(&decrease_condition);
    }
    virtual ~mha_fifo_posix_threads_t() {
        pthread_cond_destroy(&decrease_condition);
        pthread_cond_destroy(&increase_condition);
        pthread_mutex_destroy(&mutex);
    }
};
#define mha_fifo_thread_platform_implementation_t mha_fifo_posix_threads_t
#endif // _WIN32

/** Simple Mutex Guard Class */
class mha_fifo_thread_guard_t {
    mha_fifo_thread_platform_t * sync;
public:
    mha_fifo_thread_guard_t(mha_fifo_thread_platform_t * sync) {
        this->sync = sync;
        sync->aquire_mutex();
    }
    ~mha_fifo_thread_guard_t() {
        sync->release_mutex();
    }
};

/** This FIFO uses locks to synchronize access. 
 * Reading and writing can block until the operation can be executed.*/
template <class T>
class mha_fifo_lw_t : public mha_fifo_t<T> {
    /** platform specific thread synchronization */
    mha_fifo_thread_platform_t * sync;

    /** If waiting for synchronization should be aborted
     * then exception to be thrown by reader process (index 0) or writer
     * process (index 1) has to be placed here. */
    MHA_Error * error[2];
public:
    /** write specified ammount of data to the fifo.
     * If there is not enough space, then wait for more space.
     * @param data Pointer to source data.
     * @param count Number of instances to copy.
     * @throw MHA_Error when detecting a deadlock situation. */
    virtual void write(const T * data, unsigned count);

    /** read data from fifo. 
     * If there is not enough data, then wait for more data.
     * @param buf Pointer to the target buffer.
     * @param count Number of instances to copy.
     * @throw MHA_Error when detecting a deadlock situation. */
    virtual void read(T * buf, unsigned count);

    /** Create FIFO with fixed buffer size */
    explicit mha_fifo_lw_t(unsigned max_fill_count);

    /** release synchronization object */
    virtual ~mha_fifo_lw_t();

    /** Process waiting for more data or space should bail out, 
     * throwing this error 
     * @param index Use 0 for terminating reader, 1 for terminating writer. 
     * @param error MHA_Error to be thrown
     */
    virtual void set_error(unsigned index, MHA_Error * error);
};

/** The doublebuffer adapts blocksizes between an outer process, which
 * provides input data and takes output data, and an inner process,
 * which processes the input signal and generates output data using a
 * different block size than the outer process.
 * This class introduces the channels concept.
 * Input and output may have different channel counts. */
template <class FIFO>
class mha_dblbuf_t {
    /** The block size used by the outer process. */
    unsigned outer_size;

    /** The block size used by the inner process. */
    unsigned inner_size;

    /** The delay introduced by bidirectional buffer size adaptation. */
    unsigned delay;

    /** The size of each of the FIFOs */
    unsigned fifo_size;

    /** The number of input channels */
    unsigned input_channels;

    /** The number of output channels */
    unsigned output_channels;

    /** The FIFO for transporting the input signal from the outer process to
     * the inner process. */
    FIFO input_fifo;

    /** The FIFO for transporting the output signal from the inner process to
     * the outer process. */
    FIFO output_fifo;

    /** Owned copy of exception to be thrown in inner thread */
    MHA_Error * inner_error;

    /** Owned copy of exception to be thrown in outer thread */
    MHA_Error * outer_error;

public:
    virtual unsigned get_inner_size() const {return inner_size;}
    virtual unsigned get_outer_size() const {return outer_size;}
    virtual unsigned get_delay() const      {return delay;}
    virtual unsigned get_fifo_size() const  {return fifo_size;}
    virtual unsigned get_input_channels() const {return input_channels;}
    virtual unsigned get_output_channels() const {return output_channels;}
    virtual unsigned get_input_fifo_fill_count() const
    { return input_fifo.get_fill_count() / get_input_channels(); }
    virtual unsigned get_output_fifo_fill_count() const
    { return output_fifo.get_fill_count() / get_output_channels(); }
    virtual unsigned get_input_fifo_space() const
    { return input_fifo.get_available_space() / get_input_channels(); }
    virtual unsigned get_output_fifo_space() const
    { return output_fifo.get_available_space() / get_output_channels(); }
    virtual MHA_Error * get_inner_error() const 
    { return inner_error; }

    virtual void provoke_inner_error(const MHA_Error &);
    virtual void provoke_outer_error(const MHA_Error &);

    /** The datatype exchanged by the FIFO and this doublebuffer */
    typedef typename FIFO::value_type value_type;
    
    /** 
        \brief Constructor creates FIFOs with specified delay. 

        \warning The doublebuffer may block or raise an exception if the delay
        is too small. To avoid this, the delay should be
          \f[ delay >= (inner\_size - gcd(inner\_size, outer\_size)) \f].
        
        \param outer_size The block size used by the outer process.
        \param inner_size The block size used by the inner process.
        \param delay The total delay
        \param input_channels Number of input channels
        \param output_channels Number of output channels
        \param delay_data The delay consists of copies of this value. 
     */
    mha_dblbuf_t(unsigned outer_size, unsigned inner_size,
                 unsigned delay,
                 unsigned input_channels, unsigned output_channels,
                 const value_type & delay_data);

    virtual ~mha_dblbuf_t();

    /** The outer process has to call this method to propagate the input signal
     * to the inner process, and receives back the output signal.
     * @param input_signal Pointer to the input signal array.
     * @param output_signal Pointer to the output signal array.
     * @param count The number of data instances provided and expected,
     *              lower or equal to inner_size given to constructor.
     * @throw MHA_Error When count is > outer_size as given to constructor or
     *                  the underlying fifo implementation detects an error. */
    virtual void process(const value_type * input_signal,
                         value_type * output_signal,
                         unsigned count);

    /** The inner process has to call this method to receive its input signal.
     * @param input_signal Array where the doublebuffer can store the signal.
     * @throw MHA_Error When the underlying fifo implementation 
     *                  detects an error. */
    virtual void input(value_type * input_signal);
    
    /** The outer process has to call this method to deliver its output signal.
     * @param output_signal Array from which doublebuffer reads outputsignal.
     * @throw MHA_Error When the underlying fifo implementation 
     *                  detects an error. */
    virtual void output(const value_type * output_signal);
};

/** Object wrapper for mha_rt_fifo_t */
template <class T> class mha_rt_fifo_element_t {
public:
    /// Pointer to next fifo element. NULL for the last (newest) fifo element.
    mha_rt_fifo_element_t<T> *next;
    /// Indicates that this element will no longer be used and may be deleted.
    bool abandonned;
    /// Pointer to user data
    T *data;
    /** Constructor. This element assumes ownership of user data.
        @param data User data. Has to be allocated on the heap with standard
                    operator new, because it will be deleted in this element's
                    destructor. */
    mha_rt_fifo_element_t(T * data)
    { next = 0; abandonned = false; this->data = data; }
    ~mha_rt_fifo_element_t()
    { delete data; data = 0; next = 0; }
};

/**
   \brief Template class for thread safe, half real time safe fifo without explixit locks.
   
    Reading from this fifo is realtime safe, writing to it is not.
    This fifo is designed for objects that were constructed on the heap.
    It assumes ownership of these objects and calls delete on them
    when they are no longer used.
    Objects remain inside the Fifo while being used by the reader.

    A new fifo element is inserted by using #push.  The push operation
    is not real time safe, it allocates and deallocates memory.
    The latest element is retrieved by calling #poll.  This operation
    will skip fifo elements if more than one #push has been occured since
    the last poll.  To avoid skipping, call the #poll_1 operation instead.
*/
template<class T>
class mha_rt_fifo_t {
    friend class Test_mha_rt_fifo_t;
public:
    /// Construct empty fifo.
    mha_rt_fifo_t() { root = current = 0; }
    
    /// Destructor will delete all data currently in the fifo.
    ~mha_rt_fifo_t() { remove_all(); } 
    /** Retrieve the latest element in the Fifo. Will skip fifo elements
        if more than one element has been added since last poll invocation.
        Will return the same element as on last call if no elements have been
        added in the mean time.  Marks former elements as abandonned.
        @return The latest element in this Fifo.
                Returns NULL if the Fifo is empty. */
    T * poll() {
        if (root == 0) return 0;
        if (current == 0) current = root;
        while (current->next) {
            volatile mha_rt_fifo_element_t<T> * volatile oldcurrent = current;
            current = current->next;
            oldcurrent->abandonned = true;
        }
        return current->data;
    }
    /** Retrieve the next element in the Fifo, if there is one, and
        mark the previous element as abandonned.  Else, if there is no
        newer element, returns the same element as on last #poll() or
        #poll_1() invocation.
        @return The next element in this Fifo, if there is one, or the same
                as before.  Returns NULL if the Fifo is empty. */
    T * poll_1() {
        if (root == 0) return 0;
        if (current == 0) 
            current = root;
        else if (current->next) {
            volatile mha_rt_fifo_element_t<T> * volatile oldcurrent = current;
            current = current->next;
            oldcurrent->abandonned = true;
        }
        return current->data;
    }

    /** Add element to the Fifo. Deletes abandonned elements in the fifo.
        @param data The new user data to place at the end of the fifo. After
                    this invocation, the fifo is the owner of this object
                    and will delete it when it is no longer used. data must
                    have been allocated on the heap with standard operator new.
    */
    void push(T * data) {
        mha_rt_fifo_element_t<T> * element = new mha_rt_fifo_element_t<T>(data);
        if (root == 0)
            root = element;
        else {
            mha_rt_fifo_element_t<T> * previous = current;
            if (previous == 0) previous = root;
            while (previous->next) previous = previous->next;
            previous->next = element;
        }
        remove_abandonned();
    }

private:
    /// The first element in the fifo.  Deleting elements starts here.
    mha_rt_fifo_element_t<T> * root;
    /** The element most recently returned by #poll or #poll_1.
        Searching for new elements starts here. */
    mha_rt_fifo_element_t<T> * current;
    /// Deletes abandonned elements
    void remove_abandonned() {
        while (root && root->abandonned) {
            mha_rt_fifo_element_t<T> * oldroot = root;
            root = root->next;
            delete oldroot; oldroot = 0;
        }
    }
    /// Deletes all elements
    void remove_all() {
        current = 0;
        while (root) {
            mha_rt_fifo_element_t<T> * oldroot = root;
            root = root->next;
            delete oldroot; oldroot = 0;
        }
    }
};


/*
 *
 * here starts the template implementation:
 *
 */
template <class T>
void mha_fifo_t<T>::write(const T * data, unsigned count)
{
    unsigned available_space = get_available_space();
    if (count > available_space)
        throw MHA_Error(__FILE__, __LINE__,
                        "Could not write %u instances to FIFO: There"
                        " is only space for %u instances",
                        count, available_space);
    while (count--) {
        *write_ptr = *data++;
        if (write_ptr >= &buf.back())
            write_ptr = &buf.front();
        else
            ++write_ptr;
    }
}

template <class T>
void mha_fifo_t<T>::read(T * outbuf, unsigned count)
{
    unsigned available_data = get_fill_count();
    if (count > available_data)
        throw MHA_Error(__FILE__, __LINE__,
                        "Could not read %u instances from FIFO:"
                        " Only %u instances available",
                        count, available_data);
    while (count--) {
        *outbuf++ = *read_ptr;
        if (read_ptr >= &buf.back())
            read_ptr = &buf.front();
        else
            ++read_ptr;
    }
}

template <class T>
unsigned mha_fifo_t<T>::get_available_space() const
{
    return mha_fifo_t<T>::get_max_fill_count()-mha_fifo_t<T>::get_fill_count();
}


template <class T>
mha_fifo_t<T>::mha_fifo_t(unsigned max_fill_count, const T & t)
{
    // ensure that max_fill_count+1 can be stored in unsigned without overflow
    if ((max_fill_count + 1U) <= max_fill_count)
        throw MHA_Error(__FILE__,__LINE__,"Cannot create fifo of size %u",
                        max_fill_count);
    try{
        // We need to initialize all elements of the fifo as a valid instance
        // of T because we call assignment operator and eventually destructor
        // on them.  Let std::vector care about initialization and destruction.
        buf.resize(max_fill_count + 1U, t);
    } catch(const std::bad_alloc &) {
        throw MHA_Error(__FILE__,__LINE__,"Not enough memory to "
                        "allocate fifo of size %u", max_fill_count);
    }
    read_ptr = write_ptr = buf.data();
}

template <class T>
void mha_drifter_fifo_t<T>::write(const T * data, unsigned count)
{
    if (writer_started == false) {
        starting();
        writer_started = true;
    }
    if (reader_started && writer_started) {
        unsigned transferred_count = std::min(get_available_space(), count);
        mha_fifo_t<T>::write(data, transferred_count);
        if (transferred_count < count) {
            ++writer_xruns_total;
            ++writer_xruns_since_start;
            if (++writer_xruns_in_succession
                > maximum_writer_xruns_in_succession_before_stop)
                stop();
        }
        else {
            writer_xruns_in_succession = 0;
        }
    }
}

template <class T>
void mha_drifter_fifo_t<T>::read(T * buf, unsigned count)
{
    if (reader_started == false) {
        starting();
        reader_started = true;
    }
    if (reader_started && writer_started) {
        // TODO: change if get_fill_count() does not include the zeros.
        unsigned transferred_count =
            std::min(get_fill_count() - minimum_fill_count, count);
        unsigned transferred_zeros_count;
        for (transferred_zeros_count = 0;
             startup_zeros > 0 && transferred_zeros_count < transferred_count;
             --startup_zeros, ++transferred_zeros_count) {
            buf[transferred_zeros_count] = null_data;
        }
        mha_fifo_t<T>::read(buf + transferred_zeros_count,
                            transferred_count - transferred_zeros_count);
        if (transferred_count < count) {
            ++reader_xruns_total;
            ++reader_xruns_since_start;
            if (++reader_xruns_in_succession
                > maximum_reader_xruns_in_succession_before_stop)
                stop();
        }
        else {
            reader_xruns_in_succession = 0;
        }
    }
    else
        for (unsigned i = 0; i < count; ++i)
            buf[i] = null_data;
}

template <class T>
unsigned mha_drifter_fifo_t<T>::get_fill_count() const
{
    return mha_fifo_t<T>::get_fill_count() + startup_zeros;
}

template <class T>
unsigned mha_drifter_fifo_t<T>::get_available_space() const
{
    return mha_fifo_t<T>::get_available_space() - startup_zeros;
}

/** Called by #read or #write when their xrun in succession
 * counter exceeds its limit.  May also be called explicitly. */
template <class T>
void mha_drifter_fifo_t<T>::stop()
{
    writer_started = reader_started = false;
}

template <class T>
void mha_drifter_fifo_t<T>::starting()
{
    mha_fifo_t<T>::clear();
    startup_zeros = desired_fill_count;
    reader_xruns_since_start = writer_xruns_since_start = 0;
    reader_xruns_in_succession = writer_xruns_in_succession = 0;
}

template <class T>
mha_drifter_fifo_t<T>::mha_drifter_fifo_t(unsigned min_fill_count,
                                          unsigned des_fill_count,
                                          unsigned max_fill_count)
    : mha_fifo_t<T>(max_fill_count),
      minimum_fill_count(min_fill_count),
      desired_fill_count(des_fill_count),
      writer_started(false),
      reader_started(false),
      writer_xruns_total(0),
      reader_xruns_total(0),
      writer_xruns_since_start(0),
      reader_xruns_since_start(0),
      writer_xruns_in_succession(0),
      reader_xruns_in_succession(0),
      maximum_writer_xruns_in_succession_before_stop(10),
      maximum_reader_xruns_in_succession_before_stop(10),
      null_data(),
      startup_zeros(des_fill_count)
{}
template <class T>
mha_drifter_fifo_t<T>::mha_drifter_fifo_t(unsigned min_fill_count,
                                          unsigned des_fill_count,
                                          unsigned max_fill_count,
                                          const T & t)
    : mha_fifo_t<T>(max_fill_count, t),
      minimum_fill_count(min_fill_count),
      desired_fill_count(des_fill_count),
      writer_started(false),
      reader_started(false),
      writer_xruns_total(0),
      reader_xruns_total(0),
      writer_xruns_since_start(0),
      reader_xruns_since_start(0),
      writer_xruns_in_succession(0),
      reader_xruns_in_succession(0),
      maximum_writer_xruns_in_succession_before_stop(10),
      maximum_reader_xruns_in_succession_before_stop(10),
      null_data(t),
      startup_zeros(des_fill_count)
{}

#endif // __MHA_FIFO_H__

/*
 * Local variables:
 * mode: C++
 * c-basic-offset: 4
 * compile-command: "make -C .."
 * coding: utf-8-unix
 * indent-tabs-mode: nil
 * End:
 */
