/// @file Entry.cpp
/// Entry class definition

#include "Entry.h"
#include "Node.h"

using namespace cali;

cali_id_t
Entry::attribute() const
{
    return m_node ? m_node->attribute() : m_attr_id;
}

int 
Entry::count(cali_id_t attr_id) const 
{
    int res = 0;

    if (m_node) {
        for (const Node* node = m_node; node; node = node->parent())
            if (node->attribute() == attr_id)
                ++res;
    } else {
        if (m_attr_id != CALI_INV_ID && m_attr_id == attr_id)
            ++res;
    }

    return res;
}

Variant 
Entry::value() const
{
    return m_node ? m_node->data() : m_value;
}

Variant 
Entry::value(cali_id_t attr_id) const
{
    if (!m_node && attr_id == m_attr_id)
	return m_value;

    for (const Node* node = m_node; node; node = node->parent())
	if (node->attribute() == attr_id)
	    return node->data();

    return Variant();
}

const Entry Entry::empty;