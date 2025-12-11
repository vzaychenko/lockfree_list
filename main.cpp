#include <cstdlib>
#include <cassert>
#include <iostream>
#include <random>
#include <string>
#include <stdexcept>

#include "lockfree_list.hpp"

#include <set>

#define TEST_ASSERT(cond)                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            std::cerr << "Assertion failed: " << #cond << "\nFile: " << __FILE__ << "\nLine: " << __LINE__             \
                      << std::endl;                                                                                    \
            std::exit(EXIT_FAILURE);                                                                                   \
        }                                                                                                              \
    } while (0)

static void
test_single_thread_basics()
{
    std::cout << "Running test_single_thread_basics..." << std::endl;
    List<int> l;
    TEST_ASSERT(l.empty());
    TEST_ASSERT(l.size() == 0);

    for (int i = 1; i <= 5; ++i)
    {
        auto it = l.push_back(i);
        TEST_ASSERT(*it == i);
    }

    TEST_ASSERT(!l.empty());
    TEST_ASSERT(l.size() == 5);

    {
        std::vector<int> v;
        for (auto it = l.begin(); it != l.end(); ++it)
            v.push_back(*it);
        TEST_ASSERT(v.size() == 5);
        TEST_ASSERT(v[0] == 1 && v[1] == 2 && v[2] == 3 && v[3] == 4 && v[4] == 5);
    }

    {
        auto itf = l.push_front(0);
        TEST_ASSERT(*itf == 0);
        TEST_ASSERT(l.front() == 0);
        auto itb = l.emplace_back(6);
        TEST_ASSERT(*itb == 6);
        TEST_ASSERT(l.back() == 6);
        TEST_ASSERT(l.size() == 7);
    }

    {
        auto it = l.begin();
        ++it;
        auto next = l.erase(it);
        TEST_ASSERT(l.size() == 6);
        TEST_ASSERT(*l.begin() == 0);
        TEST_ASSERT(*next == 2);
    }

    {
        auto it = l.rbegin();
        TEST_ASSERT(*it == 6);
        TEST_ASSERT(l.back() == 6);
        auto it2 = l.begin();
        TEST_ASSERT(*it2 == 0);
        TEST_ASSERT(l.front() == 0);
    }

    {
        const List<int>& cref = l;
        auto             cit  = cref.cbegin();
        auto             cend = cref.cend();
        std::size_t      cnt  = 0;
        for (; cit != cend; ++cit)
            ++cnt;
        TEST_ASSERT(cnt == l.size());
    }

    {
        std::vector<int> shuffled{5, 1, 4, 3, 2};
        List<int>        s;
        for (int x : shuffled)
            s.push_back(x);

        s.sort();
        std::vector<int> v;
        for (auto it = s.begin(); it != s.end(); ++it)
            v.push_back(*it);

        TEST_ASSERT(v.size() == 5);
        TEST_ASSERT(v[0] == 1 && v[1] == 2 && v[2] == 3 && v[3] == 4 && v[4] == 5);
    }

    {
        auto it1 = l.pop_front();
        (void)it1;
        auto it2 = l.pop_back();
        (void)it2;
        TEST_ASSERT(l.size() == 4);
    }

    {
        l.clear();
        TEST_ASSERT(l.empty());
        TEST_ASSERT(l.size() == 0);
    }
    std::cout << "PASSED: test_single_thread_basics" << std::endl;
}

static void
thread_push_back(List<int>* pl, int start, int count)
{
    for (int i = 0; i < count; ++i)
        pl->push_back(start + i);
}

static void
thread_push_front(List<int>* pl, int start, int count)
{
    for (int i = 0; i < count; ++i)
        pl->push_front(start + i);
}

static void
test_multi_thread_push()
{
    std::cout << "Running test_multi_thread_push..." << std::endl;
    List<int>    l;
    unsigned int hw = std::thread::hardware_concurrency();
    if (hw == 0)
        hw = 4;

    const int     threads_back  = static_cast<int>(hw);
    const int     threads_front = static_cast<int>(hw);
    constexpr int per_thread    = 100;

    {
        std::vector<std::thread> th;
        th.reserve(threads_back);
        for (int t = 0; t < threads_back; ++t)
        {
            int base = t * 1000000;
            th.emplace_back(thread_push_back, &l, base, per_thread);
        }

        for (auto& x : th)
            x.join();

        TEST_ASSERT(l.size() == static_cast<List<int>::size_type>(threads_back * per_thread));
    }

    {
        std::vector<std::thread> th;
        th.reserve(threads_front);
        for (int t = 0; t < threads_front; ++t)
        {
            int base = 50000000 + t * 1000000;
            th.emplace_back(thread_push_front, &l, base, per_thread);
        }

        for (auto& x : th)
            x.join();

        TEST_ASSERT(l.size() == static_cast<List<int>::size_type>((threads_back + threads_front) * per_thread));
    }

    {
        std::set<int> s;
        for (auto it = l.begin(); it != l.end(); ++it)
            s.insert(*it);

        TEST_ASSERT(s.size() == l.size());
    }
    std::cout << "PASSED: test_multi_thread_push" << std::endl;
}

static void
test_iterate_and_erase_all()
{
    std::cout << "Running test_iterate_and_erase_all..." << std::endl;
    List<int> l;
    for (int i = 0; i < 100; ++i)
        l.push_back(i);

    TEST_ASSERT(l.size() == 100);

    {
        int sum = 0;
        for (auto it = l.begin(); it != l.end(); ++it)
            sum += *it;

        TEST_ASSERT(sum == 99 * 100 / 2);
    }

    {
        auto it     = l.begin();
        int  erased = 0;
        while (it != l.end())
        {
            it = l.erase(it);
            ++erased;
        }

        TEST_ASSERT(erased == 100);
        TEST_ASSERT(l.empty());
    }
    std::cout << "PASSED: test_iterate_and_erase_all" << std::endl;
}

static void
test_sort_stability_like()
{
    std::cout << "Running test_sort_stability_like..." << std::endl;
    struct P
    {
        int k;
        int v;
    };
    List<P>        l;
    std::vector<P> data;
    for (int i = 0; i < 200; ++i)
        data.push_back(P{i % 10, i});

    std::mt19937 rng(123);
    std::shuffle(data.begin(), data.end(), rng);
    for (const auto& e : data)
        l.push_back(e);

    l.sort(
        [](const P& a, const P& b)
        {
            return a.k < b.k;
        });

    int lastk = -1;
    for (auto it = l.begin(); it != l.end(); ++it)
    {
        TEST_ASSERT(it->k >= lastk);
        lastk = it->k;
    }
    std::cout << "PASSED: test_sort_stability_like" << std::endl;
}

static void
test_empty_list_operations()
{
    std::cout << "Running test_empty_list_operations..." << std::endl;
    List<int> l;
    TEST_ASSERT(l.empty());
    TEST_ASSERT(l.size() == 0);

    bool caught = false;
    try
    {
        l.front();
    }
    catch (const std::out_of_range&)
    {
        caught = true;
    }
    TEST_ASSERT(caught);

    caught = false;
    try
    {
        l.back();
    }
    catch (const std::out_of_range&)
    {
        caught = true;
    }
    TEST_ASSERT(caught);

    auto it = l.pop_front();
    TEST_ASSERT(it == l.end());
    TEST_ASSERT(l.empty());

    it = l.pop_back();
    TEST_ASSERT(it == l.end());
    TEST_ASSERT(l.empty());

    it = l.erase(l.end());
    TEST_ASSERT(it == l.end());
    std::cout << "PASSED: test_empty_list_operations" << std::endl;
}

static void
test_iterator_copy_move()
{
    std::cout << "Running test_iterator_copy_move..." << std::endl;
    List<int> l;
    l.push_back(1);
    l.push_back(2);
    l.push_back(3);

    auto it1 = l.begin();
    TEST_ASSERT(*it1 == 1);

    auto it2 = it1;
    TEST_ASSERT(*it2 == 1);
    TEST_ASSERT(it1 == it2);

    auto it3 = std::move(it1);
    TEST_ASSERT(*it3 == 1);

    it2 = it3;
    TEST_ASSERT(*it2 == 1);
    TEST_ASSERT(it2 == it3);

    auto it4 = l.begin();
    it4 = std::move(it3);
    TEST_ASSERT(*it4 == 1);
    std::cout << "PASSED: test_iterator_copy_move" << std::endl;
}

static void
test_iterator_increment_decrement()
{
    std::cout << "Running test_iterator_increment_decrement..." << std::endl;
    List<int> l;
    for (int i = 1; i <= 5; ++i)
        l.push_back(i);

    auto it = l.begin();
    TEST_ASSERT(*it == 1);

    ++it;
    TEST_ASSERT(*it == 2);

    auto it2 = it++;
    TEST_ASSERT(*it2 == 2);
    TEST_ASSERT(*it == 3);

    --it;
    TEST_ASSERT(*it == 2);

    it2 = it--;
    TEST_ASSERT(*it2 == 2);
    TEST_ASSERT(*it == 1);

    auto rit = l.rbegin();
    TEST_ASSERT(*rit == 5);

    --rit;
    TEST_ASSERT(*rit == 4);
    std::cout << "PASSED: test_iterator_increment_decrement" << std::endl;
}

static void
test_single_element()
{
    std::cout << "Running test_single_element..." << std::endl;
    List<int> l;
    l.push_back(42);

    TEST_ASSERT(!l.empty());
    TEST_ASSERT(l.size() == 1);
    TEST_ASSERT(l.front() == 42);
    TEST_ASSERT(l.back() == 42);
    TEST_ASSERT(*l.begin() == 42);

    auto it = l.begin();
    ++it;
    TEST_ASSERT(it == l.end());

    l.pop_front();
    TEST_ASSERT(l.empty());
    std::cout << "PASSED: test_single_element" << std::endl;
}

static void
test_concurrent_push_pop()
{
    std::cout << "Running test_concurrent_push_pop..." << std::endl;
    List<int>        l;
    std::atomic<int> sum{0};
    unsigned int     hw = std::thread::hardware_concurrency();
    if (hw == 0)
        hw = 4;

    const int threads = static_cast<int>(hw);

    for (int i = 0; i < 100; ++i)
        l.push_back(i);

    auto pop_worker = [&l, &sum]()
    {
        for (int i = 0; i < 10; ++i)
        {
            auto it = l.pop_front();
            if (it != l.end())
                sum.fetch_add(*it, std::memory_order_relaxed);
        }
    };

    std::vector<std::thread> th;
    th.reserve(threads);
    for (int t = 0; t < threads; ++t)
        th.emplace_back(pop_worker);

    for (auto& x : th)
        x.join();

    int remaining_sum = 0;
    for (auto it = l.begin(); it != l.end(); ++it)
        remaining_sum += *it;

    int expected_sum = 0;
    for (int i = 0; i < 100; ++i)
        expected_sum += i;

    TEST_ASSERT(sum.load() + remaining_sum == expected_sum);
    std::cout << "PASSED: test_concurrent_push_pop" << std::endl;
}

static void
test_concurrent_mixed_operations()
{
    std::cout << "Running test_concurrent_mixed_operations..." << std::endl;
    List<int>    l;
    unsigned int hw = std::thread::hardware_concurrency();
    if (hw == 0)
        hw = 4;

    const int threads = static_cast<int>(hw / 2);

    auto pusher = [&l](int start)
    {
        for (int i = 0; i < 50; ++i)
        {
            if (i % 2 == 0)
                l.push_back(start + i);
            else
                l.push_front(start + i);
        }
    };

    auto popper = [&l]()
    {
        for (int i = 0; i < 25; ++i)
        {
            if (i % 2 == 0)
                l.pop_front();
            else
                l.pop_back();
        }
    };

    std::vector<std::thread> th;
    for (int t = 0; t < threads; ++t)
        th.emplace_back(pusher, t * 1000000);
    for (int t = 0; t < threads; ++t)
        th.emplace_back(popper);

    for (auto& x : th)
        x.join();

    TEST_ASSERT(l.size() == static_cast<List<int>::size_type>(threads * 50 - threads * 25));
    std::cout << "PASSED: test_concurrent_mixed_operations" << std::endl;
}

static void
test_concurrent_iteration()
{
    std::cout << "Running test_concurrent_iteration..." << std::endl;
    List<int> l;
    for (int i = 0; i < 100; ++i)
        l.push_back(i);

    unsigned int hw = std::thread::hardware_concurrency();
    if (hw == 0)
        hw = 4;

    const int               threads = static_cast<int>(hw);
    std::vector<std::thread> th;

    auto iterator_worker = [&l]()
    {
        std::set<int> seen;
        for (auto it = l.begin(); it != l.end(); ++it)
            seen.insert(*it);
    };

    th.reserve(threads);
    for (int t = 0; t < threads; ++t)
        th.emplace_back(iterator_worker);

    for (auto& x : th)
        x.join();

    TEST_ASSERT(l.size() == 100);
    std::cout << "PASSED: test_concurrent_iteration" << std::endl;
}

static void
test_move_semantics()
{
    std::cout << "Running test_move_semantics..." << std::endl;
    List<std::string> l;
    std::string       s1 = "hello";
    std::string       s2 = "world";

    l.push_back(std::move(s1));
    l.push_back(s2);

    TEST_ASSERT(l.size() == 2);
    TEST_ASSERT(*l.begin() == "hello");
    std::cout << "PASSED: test_move_semantics" << std::endl;
}

static void
test_erase_all_variations()
{
    std::cout << "Running test_erase_all_variations..." << std::endl;
    List<int> l;
    for (int i = 0; i < 10; ++i)
        l.push_back(i);

    auto it = l.begin();
    ++it;
    ++it;
    it = l.erase(it);
    TEST_ASSERT(*it == 3);
    TEST_ASSERT(l.size() == 9);

    it = l.begin();
    it = l.erase(it);
    TEST_ASSERT(*it == 1);

    it = l.rbegin();
    it = l.erase(it);
    TEST_ASSERT(l.size() == 7);
    std::cout << "PASSED: test_erase_all_variations" << std::endl;
}

static void
test_reverse_iteration()
{
    std::cout << "Running test_reverse_iteration..." << std::endl;
    List<int> l;
    for (int i = 1; i <= 5; ++i)
        l.push_back(i);

    std::vector<int> v;
    for (auto it = l.rbegin(); it != l.rend(); --it)
    {
        v.push_back(*it);
        if (v.size() >= 5)
            break;
    }

    TEST_ASSERT(v.size() == 5);
    TEST_ASSERT(v[0] == 5 && v[1] == 4 && v[2] == 3 && v[3] == 2 && v[4] == 1);
    std::cout << "PASSED: test_reverse_iteration" << std::endl;
}

static void
test_self_assignment()
{
    std::cout << "Running test_self_assignment..." << std::endl;
    List<int> l;
    l.push_back(1);
    l.push_back(2);

    auto it = l.begin();
    it      = it;
    TEST_ASSERT(*it == 1);

    auto it2 = l.begin();
    it2      = std::move(it2);
    TEST_ASSERT(*it2 == 1);
    std::cout << "PASSED: test_self_assignment" << std::endl;
}

int
main()
{
    std::cout << "\n=== Starting Lock-Free List Tests ===\n" << std::endl;
    try
    {
        test_single_thread_basics();
        test_iterate_and_erase_all();
        test_sort_stability_like();
        test_empty_list_operations();
        test_iterator_copy_move();
        test_iterator_increment_decrement();
        test_single_element();
        test_move_semantics();
        test_erase_all_variations();
        test_reverse_iteration();
        test_self_assignment();
        test_multi_thread_push();
        test_concurrent_push_pop();
        test_concurrent_mixed_operations();
        test_concurrent_iteration();
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Exception: " << ex.what() << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << "\n=== All Tests Passed Successfully! ===" << std::endl;
    return EXIT_SUCCESS;
}