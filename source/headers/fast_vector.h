//
// Based on original work: https://github.com/sigerror/fast-vector
// Copyright (c) Vladislav Lukyanov
//
// Advanced & fixed version by Marian Krivos
//

#pragma once

#include <cassert>
#include <cstdlib>
#include <cstring> // std::memcpy()
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <initializer_list> 

// Helper functions

template <typename T>
inline void construct_range(T* begin, T* end)
{
    while (begin != end)
    {
        new (begin) T;
        begin++;
    }
}

template <typename T>
inline void copy_range(const T* begin, const T* end, T* dest)
{
    while (begin != end)
    {
        new (dest) T(*begin);
        begin++;
        dest++;
    }
}

template <typename T>
inline void move_range(const T* begin, const T* end, T* dest)
{
    while (begin != end)
    {
        *dest = std::move(*begin);
        begin++;
        dest++;
    }
}

template <typename T>
inline T* find_item(T* begin, T* end, const T& value)
{
    while (begin != end)
    {
        if (*begin == value)
            return begin;
        begin++;
    }
    return end;
}

template <typename T>
inline void destruct_range(T* begin, T* end)
{
    while (begin != end)
    {
        begin->~T();
        begin++;
    }
}

/**
 * The fast & light-weight std::vector replacement, best used for plain POD types.
 */
template <typename T, bool F = false, int A = 16>
class fast_vector
{
public:
    using size_type = std::size_t;
    using value_type = T;

    fast_vector() = default;
    fast_vector(size_t size);
    fast_vector(const fast_vector& other);
    fast_vector(std::initializer_list<T>&& other);
    fast_vector(fast_vector&& other) noexcept;
    fast_vector& operator=(const fast_vector& other);
    fast_vector& operator=(fast_vector&& other) noexcept;
    fast_vector(const T a[], const T b[]);

    ~fast_vector();

    // Element access

    T& operator[](size_type pos);
    const T& operator[](size_type pos) const;

    T& at(size_type pos);
    const T& at(size_type pos) const;

    T& front();
    const T& front() const;

    T& back();
    const T& back() const;

    T* data() noexcept;
    const T* data() const noexcept;

    // Iterators

    T* begin() noexcept;
    const T* begin() const noexcept;

    T* end() noexcept;
    const T* end() const noexcept;

    // Capacity

    bool empty() const noexcept;
    size_type size() const noexcept;
    void reserve(size_type new_cap);
    size_type capacity() const noexcept;
    void shrink_to_fit();

    // Modifiers

    void clear() noexcept;

    void push_back(const T& value);
    void push_back(T&& value);

    template< class... Args >
    void emplace_back(Args&&... args);
    
    void append(const T value[], size_t count);

    void pop_back();
    void resize(size_type count);
    bool erase(const T value);

    static void swap(fast_vector<T>& a, fast_vector<T>& b);

    static constexpr size_type grow_factor = 2;

private:
    T* m_data = nullptr;
    size_type m_size = 0;
    size_type m_capacity = 0;
};

template <typename T, bool F, int A>
fast_vector<T,F,A>::fast_vector(size_t size) :
    m_size(size),
    m_capacity(size)
{
    m_data = reinterpret_cast<T*>(std::malloc(sizeof(T) * m_capacity));

    if (!m_data)
        throw std::bad_alloc{};

    if (std::is_trivial_v<T> | F)
        memset(m_data, 0, sizeof(T) * m_capacity);
    else
        construct_range(begin(), end());
}

template <typename T, bool F, int A>
fast_vector<T,F,A>::fast_vector(std::initializer_list<T>&& other) :
    fast_vector(other.begin(), other.end())
{
}

template <typename T, bool F, int A>
fast_vector<T,F,A>::fast_vector(const T a[], const T b[])
  : m_size(b - a)
  , m_capacity(b - a)
{
    m_data = reinterpret_cast<T*>(std::malloc(sizeof(T) * m_capacity));

    if (!m_data)
        throw std::bad_alloc{};

    if (std::is_trivial_v<T>)
    {
        std::memcpy(m_data, a, sizeof(T) * m_capacity);
    }
    else
    {
        copy_range(a, b, m_data);
    }
}

template <typename T, bool F, int A>
fast_vector<T,F,A>::fast_vector(const fast_vector& other)
    : m_size(other.m_size)
    , m_capacity(other.m_size)
{
    m_data = reinterpret_cast<T*>(std::malloc(sizeof(T) * m_size));

    if (!m_data)
        throw std::bad_alloc{};

    if (std::is_trivial_v<T>)
    {
        std::memcpy(m_data, other.m_data, sizeof(T) * m_size);
    }
    else
    {
        copy_range(other.begin(), other.end(), m_data);
    }
}

template <typename T, bool F, int A>
fast_vector<T,F,A>::fast_vector(fast_vector&& other) noexcept
    : m_data(other.m_data)
    , m_size(other.m_size)
    , m_capacity(other.m_capacity)
{
    other.m_data = nullptr;
}

template <typename T, bool F, int A>
fast_vector<T,F,A>& fast_vector<T,F,A>::operator=(const fast_vector& other)
{
    this->~fast_vector<T,F,A>();

    m_size = other.m_size;
    m_capacity = other.m_size;

    m_data = reinterpret_cast<T*>(std::malloc(sizeof(T) * m_size));

    if (!m_data)
        throw std::bad_alloc{};

    if (std::is_trivial_v<T>)
    {
        std::memcpy(m_data, other.m_data, sizeof(T) * m_size);
    }
    else
    {
        copy_range(other.begin(), other.end(), m_data);
    }

    return *this;
}

template <typename T, bool F, int A>
fast_vector<T,F,A>& fast_vector<T,F,A>::operator=(fast_vector&& other) noexcept
{
    this->~fast_vector<T,F,A>();

    m_data = other.m_data;
    m_size = other.m_size;
    m_capacity = other.m_size;

    other.m_data = nullptr;

    return *this;
}

template <typename T, bool F, int A>
fast_vector<T,F,A>::~fast_vector()
{
    if (m_data)
    {
        if (!std::is_trivial_v<T>)
        {
            destruct_range(begin(), end());
        }
        std::free(m_data);
    }
}

template <typename T, bool F, int A>
void fast_vector<T,F,A>::swap(fast_vector<T>& a, fast_vector<T>& b)
{
    std::swap(a.m_data, b.m_data);
    std::swap(a.m_size, b.m_size);
    std::swap(a.m_capacity, b.m_capacity);
}

// Element access

template <typename T, bool F, int A>
T& fast_vector<T,F,A>::operator[](size_type pos)
{
    assert(pos < m_size && "Position is out of range");
    return m_data[pos];
}

template <typename T>
inline void move_and_destruct_range(T* begin, T* end, T* dest)
{
    while (begin != end)
    {
        new (dest) T(std::move(*begin));
        begin->~T();
        begin++;
        dest++;
    }
}

template <typename T, bool F, int A>
const T& fast_vector<T,F,A>::operator[](size_type pos) const
{
    assert(pos < m_size && "Position is out of range");
    return m_data[pos];
}

template <typename T, bool F, int A>
T& fast_vector<T,F,A>::at(size_type pos)
{
    if (pos >= m_size)
        throw std::range_error{"Position is out of range"};

    return operator [](pos);
}

template <typename T, bool F, int A>
const T& fast_vector<T,F,A>::at(size_type pos) const
{
    if (pos >= m_size)
        throw std::range_error{"Position is out of range"};

    return operator [](pos);
}

template <typename T, bool F, int A>
T& fast_vector<T,F,A>::front()
{
    assert(m_size > 0 && "Container is empty");
    return m_data[0];
}

template <typename T, bool F, int A>
const T& fast_vector<T,F,A>::front() const
{
    assert(m_size > 0 && "Container is empty");
    return m_data[0];
}

template <typename T, bool F, int A>
T& fast_vector<T,F,A>::back()
{
    assert(m_size > 0 && "Container is empty");
    return m_data[m_size - 1];
}

template <typename T, bool F, int A>
const T& fast_vector<T,F,A>::back() const
{
    assert(m_size > 0 && "Container is empty");
    return m_data[m_size - 1];
}

template <typename T, bool F, int A>
T* fast_vector<T,F,A>::data() noexcept
{
    return m_data;
}

template <typename T, bool F, int A>
const T* fast_vector<T,F,A>::data() const noexcept
{
    return m_data;
}

// Iterators

template <typename T, bool F, int A>
T* fast_vector<T,F,A>::begin() noexcept
{
    return m_data;
}

template <typename T, bool F, int A>
const T* fast_vector<T,F,A>::begin() const noexcept
{
    return m_data;
}

template <typename T, bool F, int A>
T* fast_vector<T,F,A>::end() noexcept
{
    return m_data + m_size;
}

template <typename T, bool F, int A>
const T* fast_vector<T,F,A>::end() const noexcept
{
    return m_data + m_size;
}

// Capacity

template <typename T, bool F, int A>
bool fast_vector<T,F,A>::empty() const noexcept
{
    return m_size == 0;
}

template <typename T, bool F, int A>
typename fast_vector<T,F,A>::size_type fast_vector<T,F,A>::size() const noexcept
{
    return m_size;
}

template <typename T, bool F, int A>
void fast_vector<T,F,A>::reserve(size_type new_cap)
{
    if (new_cap > m_capacity)
    {
        if constexpr (std::is_trivial_v<T> | F)
        {
            auto old_capacity = m_capacity;
            m_data = reinterpret_cast<T*>(std::realloc(m_data, sizeof(T) * new_cap));
            assert(m_data != nullptr && "Reallocation failed");
            // Reset new range to zero
            memset(m_data + old_capacity, 0, new_cap-old_capacity);
        }
        else
        {
            T* new_data_location = reinterpret_cast<T*>(std::malloc(sizeof(T) * new_cap));
            assert(new_data_location != nullptr && "Allocation failed");

            copy_range(begin(), end(), new_data_location);
            destruct_range(begin(), end());

            std::free(m_data);

            m_data = new_data_location;
        }

        m_capacity = new_cap;
    }
}

template <typename T, bool F, int A>
typename fast_vector<T,F,A>::size_type fast_vector<T,F,A>::capacity() const noexcept
{
    return m_capacity;
}

template <typename T, bool F, int A>
void fast_vector<T,F,A>::shrink_to_fit()
{
    if (m_size && m_size < m_capacity)
    {
        if constexpr (std::is_trivial_v<T> | F)
        {
            m_data = reinterpret_cast<T*>(std::realloc(m_data, sizeof(T) * m_size));
            assert(m_data != nullptr && "Reallocation failed");
        }
        else
        {
            T* new_data_location = reinterpret_cast<T*>(std::malloc(sizeof(T) * m_size));
            assert(new_data_location != nullptr && "Allocation failed");

            copy_range(begin(), end(), new_data_location);
            destruct_range(begin(), end());

            std::free(m_data);

            m_data = new_data_location;
        }
    }
}

// Modifiers

template <typename T, bool F, int A>
void fast_vector<T,F,A>::clear() noexcept
{
    if constexpr (!(std::is_trivial_v<T> | F))
    {
        destruct_range(begin(), end());
    }

    m_size = 0;
}

template <typename T, bool F, int A>
void fast_vector<T,F,A>::append(const T values[], size_t count)
{
    if (m_size + count >= m_capacity)
    {
        for (size_t i = 0; i < count; i++)
        {
            push_back(values[i]);
        }
    }
    else
    {
        if constexpr (std::is_trivial_v<T>)
        {
            std::memcpy(m_data+m_size, values, count*sizeof(T));
        }
        else
        {
            copy_range(values, values + count, m_data + m_size);
        }
        m_size += count;
    }
}

template <typename T, bool F, int A>
bool fast_vector<T,F,A>::erase(const T value)
{
    T* position = find_item(begin(), end(), value);
    if (position < end())
    {
        size_t count = end() - position - 1;

        if constexpr (std::is_trivial_v<T> | F)
        {
            position->~T();
            if (count > 0)
            {
                std::memcpy(position, position + 1, count*sizeof(T));
            }
        }
        else
        {
            if (count > 0)
            {
                for (size_t i = 0; i < count; i++)
                {
                    *position = std::move(*(position + 1));
                    ++position;
                }
            }
            else
            {
                position->~T();
            }
        }
        --m_size;
        return true;
    }
    return false;
}

template <typename T, bool F, int A>
void fast_vector<T,F,A>::push_back(const T& value)
{
    if (m_size == m_capacity)
    {
        reserve(m_capacity * fast_vector::grow_factor + 1);
    }

    if constexpr (std::is_trivial_v<T>)
    {
        m_data[m_size] = value;
    }
    else
    {
        new (m_data + m_size) T(value);
    }

    m_size++;
}

template <typename T, bool F, int A>
void fast_vector<T,F,A>::push_back(T&& value)
{
    if (m_size == m_capacity)
    {
        reserve(m_capacity * fast_vector::grow_factor + 1);
    }

    if constexpr (std::is_trivial_v<T>)
    {
        m_data[m_size] = value;
    }
    else
    {
        new (m_data + m_size) T(std::move(value));
    }

    m_size++;
}

template <typename T, bool F, int A>
template< class... Args >
void fast_vector<T,F,A>::emplace_back(Args&&... args)
{
    static_assert(!std::is_trivial_v<T>, "Use push_back() instead of emplace_back() with trivial types");

    if (m_size == m_capacity)
    {
        reserve(m_capacity * fast_vector::grow_factor + 1);
    }

    new (m_data + m_size) T(std::forward<Args>(args)...);

    m_size++;
}

template <typename T, bool F, int A>
void fast_vector<T,F,A>::pop_back()
{
    assert(m_size > 0 && "Container is empty");

    if constexpr (!std::is_trivial_v<T>)
    {
        m_data[m_size - 1].~T();
    }

    m_size--;
}

template <typename T, bool F, int A>
void fast_vector<T,F,A>::resize(size_type count)
{
    if (count == m_size)
        return;

    if (count > m_capacity)
    {
        reserve(count);
    }

    if constexpr (!std::is_trivial_v<T>)
    {
        if (count > m_size)
        {
            construct_range(m_data + m_size, m_data + count);
        }
        else if (count < m_size)
        {
            destruct_range(m_data + count, m_data + m_size);
        }
    }

    m_size = count;
}
