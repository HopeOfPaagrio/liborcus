/*************************************************************************
 *
 * Copyright (c) 2010, 2011 Kohei Yoshida
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 ************************************************************************/

#ifndef __ORCUS_GNUMERICCONTEXT_HPP__
#define __ORCUS_GNUMERICCONTEXT_HPP__

#include "xml_context_base.hpp"

#include <vector>
#include <boost/scoped_ptr.hpp>

namespace orcus {

namespace spreadsheet { namespace iface {

class import_factory;
class import_sheet;

}}

class gnumeric_content_xml_context : public xml_context_base
{
public:
    gnumeric_content_xml_context(session_context& session_cxt, const tokens& tokens, spreadsheet::iface::import_factory* factory);
    virtual ~gnumeric_content_xml_context();

    virtual bool can_handle_element(xmlns_id_t ns, xml_token_t name) const;
    virtual xml_context_base* create_child_context(xmlns_id_t ns, xml_token_t name);
    virtual void end_child_context(xmlns_id_t ns, xml_token_t name, xml_context_base* child);

    virtual void start_element(xmlns_id_t ns, xml_token_t name, const xml_attrs_t& attrs);
    virtual bool end_element(xmlns_id_t ns, xml_token_t name);
    virtual void characters(const pstring& str, bool transient);

private:

private:
    spreadsheet::iface::import_factory* mp_factory;
    boost::scoped_ptr<xml_context_base> mp_child;
};

}

#endif
