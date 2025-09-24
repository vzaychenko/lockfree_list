#include <cstdlib>
#include <cassert>
#include <iostream>
#include <random>

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
    List<int>    l;
    unsigned int hw = std::thread::hardware_concurrency();
    if (hw == 0)
        hw = 4;

    const int     threads_back  = static_cast<int>(hw);
    const int     threads_front = static_cast<int>(hw);
    constexpr int per_thread    = 1000;

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
}

static void
test_iterate_and_erase_all()
{
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
}

static void
test_sort_stability_like()
{
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
}

int
main()
{
    try
    {
        test_single_thread_basics();
        test_iterate_and_erase_all();
        test_sort_stability_like();
        test_multi_thread_push();
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Exception: " << ex.what() << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << "\nWork complete." << std::endl;
    return EXIT_SUCCESS;
}