#include "ring_queue.h"
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>
#include <type_traits>

using spsc::RingQueue;

// -----------------------------------------------------------------------------
// Test type that satisfies EXACTLY the TASK contract on T:
//   copy constructor + copy assignment, NO default constructor.
// Counts lifecycle events so we can prove lifetime correctness.
// -----------------------------------------------------------------------------
struct Stats {
    int constructed = 0; // built from a value
    int copied      = 0; // copy-constructed (placement new in the queue)
    int assigned    = 0; // copy-assigned (pop writes into out)
    int destroyed   = 0;
    int alive() const { return constructed + copied - destroyed; }
};

class Tracked {
public:
    Tracked(int v, Stats* s) : value(v), stats(s) { ++stats->constructed; }
    Tracked(const Tracked& o) : value(o.value), stats(o.stats) {
        ++stats->copied;
    }
    Tracked& operator=(const Tracked& o) {
        value = o.value;
        ++stats->assigned;
        return *this;
    }
    ~Tracked() { ++stats->destroyed; }
    int value;
    Stats* stats;
};

static_assert(!std::is_default_constructible_v<Tracked>,
              "Tracked must not be default-constructible");

/*
 * push/pop/observers
 */

TEST_CASE("fresh queue is empty", "[stage_1]") {
    RingQueue<int, 4> q;
    REQUIRE(q.empty());
    REQUIRE_FALSE(q.full());
    REQUIRE(q.size() == 0);
    REQUIRE(q.capacity() == 4);
}

TEST_CASE("push then pop in FIFO order", "[stage_1]") {
    RingQueue<int, 4> q;
    REQUIRE(q.push(10));
    REQUIRE(q.push(20));
    REQUIRE(q.push(30));
    REQUIRE(q.size() == 3);
    REQUIRE_FALSE(q.empty());

    int out = -1;
    REQUIRE(q.pop(out));
    REQUIRE(out == 10);
    REQUIRE(q.pop(out));
    REQUIRE(out == 20);
    REQUIRE(q.pop(out));
    REQUIRE(out == 30);
    REQUIRE(q.empty());
    REQUIRE(q.size() == 0);
}

/*
 * check object lifetime
 */

TEST_CASE("object is destroyed exactly on extraction", "[stage_2]") {
    Stats s;
    {
        RingQueue<Tracked, 4> q;
        REQUIRE(q.push(Tracked(1, &s)));
        REQUIRE(q.push(Tracked(2, &s)));

        const int before = s.destroyed;
        Tracked sink(-1, &s);
        REQUIRE(q.pop(sink));
        REQUIRE(sink.value == 1);
        REQUIRE(s.destroyed > before); // popping ran the stored object's dtor
    }
}

TEST_CASE("destructor destroys all remaining elements (no leak)", "[stage_2]") {
    Stats s;
    {
        RingQueue<Tracked, 8> q;
        for (int i = 0; i < 5; ++i)
            REQUIRE(q.push(Tracked(i, &s)));
        Tracked sink(-1, &s);
        REQUIRE(q.pop(sink)); // drain two, leave three for the destructor
        REQUIRE(q.pop(sink));
    }
    // every constructed object was destroyed exactly once
    REQUIRE(s.alive() == 0);
}

/*
 * check bounds
 */

TEST_CASE("push fails when full, does not overwrite", "[stage_3]") {
    RingQueue<int, 3> q;
    REQUIRE(q.push(1));
    REQUIRE(q.push(2));
    REQUIRE(q.push(3));
    REQUIRE(q.full());
    REQUIRE_FALSE(q.push(4));
    REQUIRE(q.size() == 3);

    int out = 0;
    REQUIRE(q.pop(out));
    REQUIRE(out == 1); // oldest still intact
}

TEST_CASE("pop fails when empty", "[stage_3]") {
    RingQueue<int, 3> q;
    int out = 0;
    REQUIRE_FALSE(q.pop(out));
}

TEST_CASE("indices wrap around correctly", "[stage_3]") {
    RingQueue<int, 4> q;
    int out = -1;
    for (int i = 0; i < 1000; ++i) {
        REQUIRE(q.push(i));
        REQUIRE(q.pop(out));
        REQUIRE(out == i);
        REQUIRE(q.empty());
    }
}

/*
 * copy constructor
 */

TEST_CASE("copy constructor preserves contents and order", "[stage_4]") {
    RingQueue<int, 4> a;
    a.push(1);
    a.push(2);
    a.push(3);

    RingQueue<int, 4> b(a);
    REQUIRE(b.size() == 3);
    int out = -1;
    REQUIRE(b.pop(out));
    REQUIRE(out == 1);
    REQUIRE(b.pop(out));
    REQUIRE(out == 2);
    REQUIRE(b.pop(out));
    REQUIRE(out == 3);
}

TEST_CASE("copy is independent of the original", "[stage_4]") {
    RingQueue<int, 4> a;
    a.push(1);
    a.push(2);

    RingQueue<int, 4> b(a);
    int out = -1;
    b.pop(out);             // mutate the copy
    REQUIRE(a.size() == 2); // original untouched
}

TEST_CASE("copy constructor does not leak or double-destroy", "[stage_4]") {
    Stats s;
    {
        RingQueue<Tracked, 4> a;
        a.push(Tracked(1, &s));
        a.push(Tracked(2, &s));
        RingQueue<Tracked, 4> b(a); // deep copy via copy constructor
    }
    REQUIRE(s.alive() == 0);
}

/*
 * copy assignment
 */

TEST_CASE("copy assignment replaces contents", "[stage_5]") {
    RingQueue<int, 4> a;
    a.push(7);
    a.push(8);

    RingQueue<int, 4> b;
    b.push(99);
    b = a;

    REQUIRE(b.size() == 2);
    int out = -1;
    REQUIRE(b.pop(out));
    REQUIRE(out == 7);
    REQUIRE(b.pop(out));
    REQUIRE(out == 8);
}

TEST_CASE("self-assignment is safe", "[stage_5]") {
    RingQueue<int, 4> a;
    a.push(1);
    a.push(2);
    RingQueue<int, 4>& ref = a;
    a                      = ref; // must not corrupt or crash
    REQUIRE(a.size() == 2);
    int out = -1;
    REQUIRE(a.pop(out));
    REQUIRE(out == 1);
}

TEST_CASE("copy assignment does not leak or double-destroy", "[stage_5]") {
    Stats s;
    {
        RingQueue<Tracked, 4> a;
        a.push(Tracked(1, &s));
        RingQueue<Tracked, 4> b;
        b.push(Tracked(2, &s));
        b.push(Tracked(3, &s));
        b = a; // b's old elements destroyed, a's copied in
    }
    REQUIRE(s.alive() == 0);
}

/*
 * concurrency stress
 */
TEST_CASE("single producer / single consumer under load", "[stage_6]") {
    constexpr int COUNT = 200000;
    RingQueue<int, 64> q;

    bool order_ok{true};
    int received = 0;

    std::thread consumer([&] {
        int expected = 0;
        while (expected < COUNT) {
            int x;
            if (q.pop(x)) {
                if (x != expected)
                    order_ok = false;
                ++expected;
                ++received;
            }
        }
    });

    std::thread producer([&] {
        for (int i = 0; i < COUNT; ++i) {
            while (!q.push(i)) {
                /* spin until have space to push to */
            }
        }
    });

    // pause main thread until producer and consumer are finished
    producer.join();
    consumer.join();

    REQUIRE(order_ok); // every item arrived, strictly in FIFO order
    REQUIRE(received == COUNT);
    REQUIRE(q.empty());
}

// Move this guard DOWN past each test group to check it specifically.
#if defined(RING_RUN_ALL_TESTS)

#endif // RING_RUN_ALL_TESTS
