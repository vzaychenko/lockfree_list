#pragma once

#include <atomic>
#include <thread>
#include <utility>
#include <functional>

template<typename T>
class List
{
    struct Node;
    using NodePtr = Node*;

    struct Link
    {
        NodePtr       ptr;
        std::uint64_t tag;
    };

    static_assert(std::is_trivially_copyable_v<Link>);

    struct Node
    {
    private:
        explicit Node(const T& d)
            : data(d)
        {
        }

        explicit Node(T&& d)
            : data(std::move(d))
        {
        }

        Node() = default;

    public:
        static NodePtr
        Create()
        {
            return new Node;
        }

        static NodePtr
        Create(const T& d)
        {
            return new Node(d);
        }

        static NodePtr
        Create(T&& d)
        {
            return new Node(std::move(d));
        }

        bool
        Insert(NodePtr const newNode)
        {
            for (;;)
            {
                Link prevL = m_prev.load(std::memory_order_acquire);
                if (!prevL.ptr)
                {
                    std::this_thread::yield();
                    continue;
                }

                Link nextL = m_next.load(std::memory_order_acquire);
                while (!nextL.ptr)
                {
                    std::this_thread::yield();
                    nextL = m_next.load(std::memory_order_acquire);
                }

                if (!IsLinked(nextL.ptr, prevL.ptr))
                {
                    std::this_thread::yield();
                    continue;
                }

                Link expectedPrev = prevL;
                Link lockPrev{nullptr, expectedPrev.tag + 1};
                if (!m_prev.compare_exchange_weak(
                        expectedPrev,
                        lockPrev,
                        std::memory_order_acq_rel,
                        std::memory_order_acquire))
                {
                    continue;
                }

                newNode->m_prev.store(Link{prevL.ptr, 0}, std::memory_order_release);
                newNode->m_next.store(Link{this, 0}, std::memory_order_release);

                if (prevL.ptr)
                {
                    Link prevNext = prevL.ptr->m_next.load(std::memory_order_acquire);
                    if (prevNext.ptr != this || !prevL.ptr->m_next.compare_exchange_strong(
                                                    prevNext,
                                                    Link{newNode, prevNext.tag + 1},
                                                    std::memory_order_acq_rel,
                                                    std::memory_order_acquire))
                    {
                        m_prev.store(Link{prevL.ptr, lockPrev.tag + 1}, std::memory_order_release);
                        std::this_thread::yield();
                        continue;
                    }
                }

                m_refCounter.fetch_add(1, std::memory_order_acq_rel);
                m_prev.store(Link{newNode, lockPrev.tag + 1}, std::memory_order_release);
                return true;
            }
        }

        std::pair<bool, NodePtr>
        Remove()
        {
            for (;;)
            {
                Link nextL = m_next.load(std::memory_order_acquire);
                if (!nextL.ptr)
                {
                    std::this_thread::yield();
                    continue;
                }

                Link prevL = m_prev.load(std::memory_order_acquire);
                if (!prevL.ptr)
                {
                    std::this_thread::yield();
                    continue;
                }

                if (!IsLinked(nextL.ptr, prevL.ptr))
                {
                    std::this_thread::yield();
                    continue;
                }

                Link expectedNext = nextL;
                Link lockNext{nullptr, expectedNext.tag + 1};
                if (!m_next.compare_exchange_weak(
                        expectedNext,
                        lockNext,
                        std::memory_order_acq_rel,
                        std::memory_order_acquire))
                {
                    std::this_thread::yield();
                    continue;
                }

                Link expectedPrev = prevL;
                Link lockPrev{nullptr, expectedPrev.tag + 1};
                if (!m_prev.compare_exchange_weak(
                        expectedPrev,
                        lockPrev,
                        std::memory_order_acq_rel,
                        std::memory_order_acquire))
                {
                    m_next.store(Link{nextL.ptr, lockNext.tag + 1}, std::memory_order_release);
                    std::this_thread::yield();
                    continue;
                }

                if (nextL.ptr)
                {
                    if (Link nextPrev = nextL.ptr->m_prev.load(std::memory_order_acquire);
                        nextPrev.ptr != this || !nextL.ptr->m_prev.compare_exchange_strong(
                                                    nextPrev,
                                                    Link{prevL.ptr, nextPrev.tag + 1},
                                                    std::memory_order_acq_rel,
                                                    std::memory_order_acquire))
                    {
                        m_next.store(Link{nextL.ptr, lockNext.tag + 1}, std::memory_order_release);
                        m_prev.store(Link{prevL.ptr, lockPrev.tag + 1}, std::memory_order_release);
                        std::this_thread::yield();
                        continue;
                    }
                }

                if (prevL.ptr)
                {
                    Link prevNext = prevL.ptr->m_next.load(std::memory_order_acquire);
                    for (;;)
                    {
                        if (prevNext.ptr != this)
                            break;

                        if (Link desired{nextL.ptr, prevNext.tag + 1}; prevL.ptr->m_next.compare_exchange_weak(
                                prevNext,
                                desired,
                                std::memory_order_acq_rel,
                                std::memory_order_acquire))
                        {
                            break;
                        }

                        std::this_thread::yield();
                    }
                }

                m_next.store(Link{nextL.ptr, lockNext.tag + 1}, std::memory_order_release);
                m_prev.store(Link{prevL.ptr, lockPrev.tag + 1}, std::memory_order_release);
                m_refCounter.fetch_sub(1, std::memory_order_acq_rel);

                return std::make_pair(true, nextL.ptr);
            }
        }

        bool
        IsLinked(const NodePtr next, const NodePtr prev) const
        {
            NodePtr    nextThis = next ? next->m_prev.load(std::memory_order_acquire).ptr : nullptr;
            NodePtr    prevThis = prev ? prev->m_next.load(std::memory_order_acquire).ptr : nullptr;
            const bool okNext   = (!next) || (!nextThis || nextThis == this);
            const bool okPrev   = (!prev) || (!prevThis || prevThis == this);

            return okNext && okPrev;
        }

        std::atomic<int>  m_refCounter{1};
        std::atomic<Link> m_next{Link{nullptr, 0}};
        std::atomic<Link> m_prev{Link{nullptr, 0}};
        T                 data;
    };

    static void
    DecRef(std::atomic<NodePtr>& node)
    {
        NodePtr nodePtr = node.load(std::memory_order_acquire);
        if (nodePtr && nodePtr->m_refCounter.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            delete nodePtr;
            node.store(nullptr, std::memory_order_release);
        }
    }

    static void
    DecRef(NodePtr node)
    {
        if (node && node->m_refCounter.fetch_sub(1, std::memory_order_acq_rel) == 1)
            delete node;
    }

    static void
    IncRef(std::atomic<NodePtr>& node)
    {
        if (NodePtr nodePtr = node.load(std::memory_order_acquire))
            nodePtr->m_refCounter.fetch_add(1, std::memory_order_acq_rel);
    }

    static NodePtr
    WaitNext(NodePtr node)
    {
        if (!node)
            return node;

        Link l = node->m_next.load(std::memory_order_acquire);
        while (!l.ptr)
        {
            std::this_thread::yield();
            l = node->m_next.load(std::memory_order_acquire);
        }

        if (l.ptr)
            l.ptr->m_refCounter.fetch_add(1, std::memory_order_acq_rel);

        return l.ptr;
    }

    static NodePtr
    WaitPrev(NodePtr node)
    {
        if (!node)
            return node;

        Link l = node->m_prev.load(std::memory_order_acquire);
        while (!l.ptr)
        {
            std::this_thread::yield();
            l = node->m_prev.load(std::memory_order_acquire);
        }

        if (l.ptr)
            l.ptr->m_refCounter.fetch_add(1, std::memory_order_acq_rel);

        return l.ptr;
    }

public:
    using size_type = std::size_t;

    class iterator
    {
    public:
        iterator() = default;

        ~iterator()
        {
            DecRef(m_ptr);
        }

        iterator(const iterator& that)
            : m_ptr(that.m_ptr.load(std::memory_order_acquire))
        {
            IncRef(m_ptr);
        }

        iterator(iterator&& that) noexcept
            : m_ptr(that.m_ptr.load(std::memory_order_acquire))
        {
            that.m_ptr.store(nullptr, std::memory_order_release);
        }

        iterator&
        operator=(const iterator& that)
        {
            IncRef(that.m_ptr);
            DecRef(m_ptr);
            m_ptr.store(that.m_ptr.load(std::memory_order_acquire), std::memory_order_release);

            return *this;
        }

        iterator&
        operator=(iterator&& that) noexcept
        {
            DecRef(m_ptr);
            m_ptr.store(that.m_ptr.load(std::memory_order_acquire), std::memory_order_release);
            that.m_ptr.store(nullptr, std::memory_order_release);

            return *this;
        }

        const iterator&
        operator++() const
        {
            const NodePtr ptr = m_ptr.load(std::memory_order_acquire);
            m_ptr.store(WaitNext(ptr), std::memory_order_release);
            DecRef(ptr);

            return *this;
        }

        iterator&
        operator++()
        {
            const NodePtr ptr = m_ptr.load(std::memory_order_acquire);
            m_ptr.store(WaitNext(ptr), std::memory_order_release);
            DecRef(ptr);

            return *this;
        }

        const iterator
        operator++(int) const
        {
            const NodePtr  ptr = m_ptr.load(std::memory_order_acquire);
            const iterator it(m_ptr.load(std::memory_order_acquire));
            m_ptr.store(WaitNext(ptr), std::memory_order_release);
            DecRef(ptr);

            return it;
        }

        iterator
        operator++(int)
        {
            const NodePtr ptr = m_ptr.load(std::memory_order_acquire);
            iterator      it(m_ptr.load(std::memory_order_acquire));
            m_ptr.store(WaitNext(ptr), std::memory_order_release);
            DecRef(ptr);

            return it;
        }

        const iterator&
        operator--() const
        {
            const NodePtr ptr = m_ptr.load(std::memory_order_acquire);
            m_ptr.store(WaitPrev(ptr), std::memory_order_release);
            DecRef(ptr);

            return *this;
        }

        iterator&
        operator--()
        {
            const NodePtr ptr = m_ptr.load(std::memory_order_acquire);
            m_ptr.store(WaitPrev(ptr), std::memory_order_release);
            DecRef(ptr);

            return *this;
        }

        const iterator
        operator--(int) const
        {
            const NodePtr  ptr = m_ptr.load(std::memory_order_acquire);
            const iterator it(m_ptr.load(std::memory_order_acquire));
            m_ptr.store(WaitPrev(ptr), std::memory_order_release);
            DecRef(ptr);

            return it;
        }

        iterator
        operator--(int)
        {
            const NodePtr ptr = m_ptr.load(std::memory_order_acquire);
            iterator      it(m_ptr.load(std::memory_order_acquire));
            m_ptr.store(WaitPrev(ptr), std::memory_order_release);
            DecRef(ptr);

            return it;
        }

        T&
        operator*() const
        {
            return m_ptr.load(std::memory_order_acquire)->data;
        }

        T*
        operator->() const
        {
            return &(m_ptr.load(std::memory_order_acquire)->data);
        }

        bool
        operator==(const iterator& it) const
        {
            return m_ptr.load(std::memory_order_acquire) == it.m_ptr.load(std::memory_order_acquire);
        }

        bool
        operator!=(const iterator& it) const
        {
            return m_ptr.load(std::memory_order_acquire) != it.m_ptr.load(std::memory_order_acquire);
        }

    private:
        explicit iterator(NodePtr ptr)
            : m_ptr(ptr)
        {
            IncRef(m_ptr);
        }

        NodePtr
        handle() const
        {
            return m_ptr.load(std::memory_order_acquire);
        }

        mutable std::atomic<NodePtr> m_ptr = nullptr;

        friend class List<T>;
    };

    List()
        : m_last(Node::Create())
        , m_size(0)
    {
        m_last->m_prev.store(Link{m_last, 0}, std::memory_order_release);
        m_last->m_next.store(Link{m_last, 0}, std::memory_order_release);
    }

    virtual ~List()
    {
        clear();
        delete m_last;
    }

    iterator
    begin()
    {
        return iterator(m_last->m_next.load(std::memory_order_acquire).ptr);
    }

    T&
    front()
    {
        return *begin();
    }

    T&
    back()
    {
        return *rbegin();
    }

    const iterator
    cbegin() const
    {
        return iterator(m_last->m_next.load(std::memory_order_acquire).ptr);
    }

    const iterator
    cend() const
    {
        return iterator(m_last);
    }

    iterator
    end()
    {
        return iterator(m_last);
    }

    iterator
    rbegin()
    {
        return iterator(m_last->m_prev.load(std::memory_order_acquire).ptr);
    }

    iterator
    rend()
    {
        return iterator(m_last);
    }

    iterator
    pop_front()
    {
        for (;;)
        {
            auto it = begin();
            if (it != end())
            {
                if (Erase(it).first)
                    return it;
            }
            else
                return it;
        }
    }

    iterator
    pop_back()
    {
        for (;;)
        {
            auto it = rbegin();
            if (it != end())
            {
                if (Erase(it).first)
                    return it;
            }
            else
                return it;
        }
    }

    iterator
    push_front(const T& data)
    {
        iterator      it;
        const NodePtr newNode = Node::Create(data);
        do
        {
            it = Insert(begin(), newNode);
        } while (it == end());

        return it;
    }

    iterator
    push_front(T&& data)
    {
        iterator      it;
        const NodePtr newNode = Node::Create(std::move(data));
        do
        {
            it = Insert(begin(), newNode);
        } while (it == end());

        return it;
    }

    iterator
    push_back(const T& data)
    {
        iterator      it;
        const NodePtr newNode = Node::Create(data);
        do
        {
            it = Insert(end(), newNode);
        } while (it == end());

        return it;
    }

    iterator
    push_back(T&& data)
    {
        iterator      it;
        const NodePtr newNode = Node::Create(std::move(data));
        do
        {
            it = Insert(end(), newNode);
        } while (it == end());

        return it;
    }

    iterator
    emplace_back(const T& data)
    {
        return push_back(data);
    }

    iterator
    emplace_back(T&& data)
    {
        return push_back(std::move(data));
    }

    iterator
    erase(iterator it)
    {
        return Erase(it).second;
    }

    void
    clear()
    {
        auto it = begin();
        while (it != end())
            it = erase(it);
    }

    bool
    empty() const
    {
        return cbegin() == cend();
    }

    size_type
    size() const
    {
        return m_size.load(std::memory_order_acquire);
    }

    // this method isn't thread-safe
    template<typename Compare = std::less<T>>
    void
    sort(Compare comp = std::less<T>())
    {
        const size_t s = m_size.load(std::memory_order_acquire);
        if (s > 1)
        {
            for (size_t i = 0; i < s - 1; i++)
            {
                auto iter1 = begin();
                auto iter2 = iter1;
                ++iter2;
                bool shouldBreak = true;
                for (size_t j = 0; j < s - i - 1; j++, ++iter1, ++iter2)
                {
                    NodePtr item1 = iter1.m_ptr.load(std::memory_order_acquire);
                    NodePtr item2 = iter2.m_ptr.load(std::memory_order_acquire);
                    if (comp(item2->data, item1->data))
                    {
                        T tmp       = std::move(item1->data);
                        item1->data = std::move(item2->data);
                        item2->data = std::move(tmp);
                        shouldBreak = false;
                    }
                }

                if (shouldBreak)
                    break;
            }
        }
    }

private:
    iterator
    Insert(const iterator it, NodePtr node)
    {
        NodePtr h = it.handle();
        if (h && h->Insert(node))
        {
            m_size.fetch_add(1, std::memory_order_acq_rel);
            return iterator(node);
        }

        return end();
    }

    std::pair<bool, iterator>
    Erase(iterator it)
    {
        NodePtr                  h   = it.handle();
        const size_t             s   = m_size.load(std::memory_order_acquire);
        std::pair<bool, NodePtr> res = h->Remove();
        if (res.first && s > 0)
            m_size.fetch_sub(1, std::memory_order_acq_rel);

        return std::make_pair(res.first, iterator(res.second));
    }

    NodePtr             m_last;
    std::atomic<size_t> m_size;
};