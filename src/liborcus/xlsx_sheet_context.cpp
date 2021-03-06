/*************************************************************************
 *
 * Copyright (c) 2011-2012 Kohei Yoshida
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

#include "xlsx_sheet_context.hpp"
#include "ooxml_global.hpp"
#include "ooxml_schemas.hpp"
#include "ooxml_token_constants.hpp"
#include "ooxml_namespace_types.hpp"
#include "orcus/exception.hpp"
#include "orcus/global.hpp"
#include "orcus/spreadsheet/import_interface.hpp"
#include "orcus/measurement.hpp"

#include <algorithm>
#include <sstream>

using namespace std;

namespace orcus {

namespace {

class col_attr_parser : public std::unary_function<void, xml_token_attr_t>
{
    long m_min;
    long m_max;
    double m_width;
    bool m_custom_width;
    bool m_contains_width;
    bool m_hidden;
public:
    col_attr_parser() : m_min(0), m_max(0), m_width(0.0), m_custom_width(false),
                        m_contains_width(false), m_hidden(false) {}

    void operator() (const xml_token_attr_t& attr)
    {
        if (attr.value.empty())
            return;

        const char* p = attr.value.get();
        const char* p_end = p + attr.value.size();

        switch (attr.name)
        {
            case XML_min:
                m_min = to_long(p, p_end);
            break;
            case XML_max:
                m_max = to_long(p, p_end);
            break;
            case XML_width:
                m_width = to_double(p, p_end);
                m_contains_width = true;
            break;
            case XML_customWidth:
                m_custom_width = to_long(p, p_end);
            break;
            case XML_hidden:
                m_hidden = to_long(p, p_end);
            break;
            default:
                ;
        }
    }

    long get_min() const { return m_min; }
    long get_max() const { return m_max; }
    double get_width() const { return m_width; }
    bool is_custom_width() const { return m_custom_width; }
    bool contains_width() const { return m_contains_width; }
    bool is_hidden() const { return m_hidden; }
};

class row_attr_parser : public std::unary_function<void, xml_token_attr_t>
{
    spreadsheet::row_t m_row;
    length_t m_height;
    bool m_contains_address;
    bool m_hidden;
public:
    row_attr_parser() : m_row(0), m_contains_address(false), m_hidden(false) {}
    void operator() (const xml_token_attr_t& attr)
    {
        switch (attr.name)
        {
            case XML_r:
            {
                // row index
                m_row = static_cast<spreadsheet::row_t>(
                    strtoul(attr.value.str().c_str(), NULL, 10));
                if (!m_row)
                    throw xml_structure_error("row number can never be zero!");

                m_row -= 1; // from 1-based to 0-based.
                m_contains_address = true;
            }
            break;
            case XML_ht:
            {
                const pstring& s = attr.value;
                m_height.value = to_double(s.get(), s.get()+s.size(), NULL);
                m_height.unit = length_unit_point;
            }
            case XML_hidden:
            {
                const pstring& s = attr.value;
                m_hidden = to_long(s.get(), s.get() + s.size());
            }
            break;
            default:
                ;
        }
    }

    spreadsheet::row_t get_row() const { return m_row; }

    length_t get_height() const { return m_height; }

    bool contains_address() const { return m_contains_address; }

    bool is_hidden() const { return m_hidden; }
};

class cell_attr_parser : public std::unary_function<xml_token_attr_t, void>
{
    struct address
    {
        spreadsheet::row_t row;
        spreadsheet::col_t col;
        address(spreadsheet::row_t _row, spreadsheet::col_t _col) : row(_row), col(_col) {}
    };

    xlsx_sheet_context::cell_type m_type;
    address m_address;
    size_t m_xf;
    bool m_contains_address;

public:
    cell_attr_parser() :
        m_type(xlsx_sheet_context::cell_type_value),
        m_address(0,0),
        m_xf(0),
        m_contains_address(false) {}

    void operator() (const xml_token_attr_t& attr)
    {
        switch (attr.name)
        {
            case XML_r:
                // cell address in A1 notation.
                m_address = to_cell_address(attr.value);
                m_contains_address = true;
            break;
            case XML_t:
                // cell type
                m_type = to_cell_type(attr.value);
            break;
            case XML_s:
                // cell style
                m_xf = strtoul(attr.value.get(), NULL, 10);
            break;
        }
    }

    xlsx_sheet_context::cell_type get_cell_type() const { return m_type; }

    spreadsheet::row_t get_row() const { return m_address.row; }
    spreadsheet::col_t get_col() const { return m_address.col; }
    size_t get_xf() const { return m_xf; }
    bool contains_address() const { return m_contains_address; }

private:
    xlsx_sheet_context::cell_type to_cell_type(const pstring& s) const
    {
        xlsx_sheet_context::cell_type t = xlsx_sheet_context::cell_type_value;
        if (s == "s")
            t = xlsx_sheet_context::cell_type_string;
        else if (s == "str")
            // formula string
            t = xlsx_sheet_context::cell_type_formula_string;
        else if (s == "b")
            // boolean
            t = xlsx_sheet_context::cell_type_boolean;
        else if (s == "e")
            // error
            t = xlsx_sheet_context::cell_type_error;
        else if (s == "inlineStr")
            t = xlsx_sheet_context::cell_type_inline_string;

        return t;
    }

    address to_cell_address(const pstring& s) const
    {
        spreadsheet::row_t row = 0;
        spreadsheet::col_t col = 0;
        const char* p = s.get();
        size_t n = s.size();
        for (size_t i = 0; i < n; ++i, ++p)
        {
            char c = *p;
            if ('A' <= c && c <= 'Z')
            {
                col *= 26;
                col += static_cast<spreadsheet::col_t>(c - 'A' + 1);
            }
            else if ('0' <= c && c <= '9')
            {
                row *= 10;
                row += static_cast<spreadsheet::row_t>(c - '0');
            }
            else
            {
                std::ostringstream os;
                os << "invalid cell address: " << s;
                throw xml_structure_error(os.str());
            }
        }

        if (!row || !col)
        {
            std::ostringstream os;
            os << "invalid cell address: " << s;
            throw xml_structure_error(os.str());
        }

        return address(row-1, col-1); // switch from 1-based to 0-based.
    }
};

class formula_attr_parser : public std::unary_function<xml_token_attr_t, void>
{
    pstring m_type;
    pstring m_ref;
    int m_shared_index;
public:
    formula_attr_parser() : m_shared_index(-1) {}

    void operator() (const xml_token_attr_t& attr)
    {
        switch (attr.name)
        {
            case XML_t:
                m_type = attr.value;
            break;
            case XML_ref:
                m_ref = attr.value;
            break;
            case XML_si:
                m_shared_index = strtoul(attr.value.get(), NULL, 10);
            break;
        }
    }

    pstring get_type() const { return m_type; }
    pstring get_ref() const { return m_ref; }
    int get_shared_index() const { return m_shared_index; }
};

}

xlsx_sheet_context::xlsx_sheet_context(session_context& session_cxt, const tokens& tokens, spreadsheet::iface::import_sheet* sheet) :
    xml_context_base(session_cxt, tokens),
    mp_sheet(sheet),
    m_cur_row(-1),
    m_cur_col(-1),
    m_cur_cell_type(cell_type_value),
    m_cur_cell_xf(0),
    m_cur_shared_formula_id(-1)
{
}

xlsx_sheet_context::~xlsx_sheet_context()
{
}

bool xlsx_sheet_context::can_handle_element(xmlns_id_t ns, xml_token_t name) const
{
    return true;
}

xml_context_base* xlsx_sheet_context::create_child_context(xmlns_id_t ns, xml_token_t name)
{
    return NULL;
}

void xlsx_sheet_context::end_child_context(xmlns_id_t ns, xml_token_t name, xml_context_base* child)
{
}

void xlsx_sheet_context::start_element(xmlns_id_t ns, xml_token_t name, const xml_attrs_t& attrs)
{
    xml_token_pair_t parent = push_stack(ns, name);

    switch (name)
    {
        case XML_worksheet:
        {
            print_attrs(get_tokens(), attrs);
        }
        break;
        case XML_cols:
            xml_element_expected(parent, NS_ooxml_xlsx, XML_worksheet);
        break;
        case XML_col:
        {
            xml_element_expected(parent, NS_ooxml_xlsx, XML_cols);
            col_attr_parser func;
            func = for_each(attrs.begin(), attrs.end(), func);
            cout << "column: min=" << func.get_min() << " max=" << func.get_max() <<
                " width=" << func.get_width() << " custom=" << func.is_custom_width() << endl;

            spreadsheet::iface::import_sheet_properties* sheet_props = mp_sheet->get_sheet_properties();
            if (sheet_props)
            {
                double width = func.get_width();
                bool contains_width = func.contains_width();
                bool hidden = func.is_hidden();
                for (spreadsheet::col_t col = func.get_min(); col <= func.get_max(); ++col)
                {
                    if (contains_width)
                        sheet_props->set_column_width(col-1, width, length_unit_xlsx_column_digit);
                    sheet_props->set_column_hidden(col-1, hidden);
                }
            }
        }
        break;
        case XML_dimension:
            xml_element_expected(parent, NS_ooxml_xlsx, XML_worksheet);
        break;
        case XML_pageMargins:
            xml_element_expected(parent, NS_ooxml_xlsx, XML_worksheet);
        break;
        case XML_sheetViews:
            xml_element_expected(parent, NS_ooxml_xlsx, XML_worksheet);
        break;
        case XML_sheetView:
            xml_element_expected(parent, NS_ooxml_xlsx, XML_sheetViews);
        break;
        case XML_selection:
            xml_element_expected(parent, NS_ooxml_xlsx, XML_sheetView);
        break;
        case XML_sheetData:
            xml_element_expected(parent, NS_ooxml_xlsx, XML_worksheet);
        break;
        case XML_sheetFormatPr:
            xml_element_expected(parent, NS_ooxml_xlsx, XML_worksheet);
        break;
        case XML_row:
        {
            xml_element_expected(parent, NS_ooxml_xlsx, XML_sheetData);
            row_attr_parser func;
            func = for_each(attrs.begin(), attrs.end(), func);
            if (func.contains_address())
                m_cur_row = func.get_row();
            else
                ++m_cur_row;

            m_cur_col = -1;

            spreadsheet::iface::import_sheet_properties* sheet_props = mp_sheet->get_sheet_properties();
            if (sheet_props)
            {
                length_t ht = func.get_height();
                if (ht.unit != length_unit_unknown)
                    sheet_props->set_row_height(m_cur_row, ht.value, ht.unit);

                bool hidden = func.is_hidden();
                sheet_props->set_row_hidden(m_cur_row, hidden);
            }
        }
        break;
        case XML_c:
        {
            xml_element_expected(parent, NS_ooxml_xlsx, XML_row);
            cell_attr_parser func;
            func = for_each(attrs.begin(), attrs.end(), func);

            if (func.contains_address())
            {
                if (m_cur_row != func.get_row())
                    throw xml_structure_error("row numbers differ!");

                m_cur_col = func.get_col();
            }
            else
            {
                ++m_cur_col;
            }

            m_cur_cell_type = func.get_cell_type();
            m_cur_cell_xf = func.get_xf();
        }
        break;
        case XML_f:
        {
            xml_element_expected(parent, NS_ooxml_xlsx, XML_c);
            formula_attr_parser func;
            func = for_each(attrs.begin(), attrs.end(), func);
            m_cur_formula_type = func.get_type();
            m_cur_formula_ref = func.get_ref();
            m_cur_shared_formula_id = func.get_shared_index();
        }
        break;
        case XML_v:
            xml_element_expected(parent, NS_ooxml_xlsx, XML_c);
        break;
        default:
            warn_unhandled();
    }

}

bool xlsx_sheet_context::end_element(xmlns_id_t ns, xml_token_t name)
{
    switch (name)
    {
        case XML_c:
            end_element_cell();
        break;
        case XML_f:
        {
#if 0
            cout << "cell: row=" << m_cur_row << "; col=" << m_cur_col << "; ";

            if (m_cur_shared_formula_id >= 0)
            {
                cout << "shared formula: index = " << m_cur_shared_formula_id;
                if (!m_cur_str.empty())
                    cout << "; " << m_cur_str;
                cout << endl;
            }
            else
                cout << "formula: " << m_cur_str << endl;
#endif

            m_cur_formula_str = m_cur_str;
        }
        case XML_v:
            m_cur_value = m_cur_str;
        break;
    }

    m_cur_str.clear();
    return pop_stack(ns, name);
}

void xlsx_sheet_context::characters(const pstring& str, bool transient)
{
    m_cur_str = str;
}

void xlsx_sheet_context::end_element_cell()
{
    if (!m_cur_formula_str.empty())
    {
        if (m_cur_formula_type == "shared" && m_cur_shared_formula_id >= 0)
        {
            // shared formula expression
            mp_sheet->set_shared_formula(
                m_cur_row, m_cur_col, spreadsheet::xlsx_2007, m_cur_shared_formula_id,
                m_cur_formula_str.get(), m_cur_formula_str.size(),
                m_cur_formula_ref.get(), m_cur_formula_ref.size());
        }
        else if (m_cur_formula_type == "array")
        {
            // array formula expression
            mp_sheet->set_array_formula(
                m_cur_row, m_cur_col, spreadsheet::xlsx_2007,
                m_cur_formula_str.get(), m_cur_formula_str.size(),
                m_cur_formula_ref.get(), m_cur_formula_ref.size());
        }
        else
        {
            // normal (non-shared) formula expression
            mp_sheet->set_formula(
                m_cur_row, m_cur_col, spreadsheet::xlsx_2007, m_cur_formula_str.get(),
                m_cur_formula_str.size());
        }
    }
    else if (m_cur_formula_type == "shared" && m_cur_shared_formula_id >= 0)
    {
        // shared formula without formula expression
        mp_sheet->set_shared_formula(m_cur_row, m_cur_col, m_cur_shared_formula_id);
    }
    else if (!m_cur_value.empty())
    {
        switch (m_cur_cell_type)
        {
            case cell_type_string:
            {
                // string cell
                size_t str_id = strtoul(m_cur_value.get(), NULL, 10);
                mp_sheet->set_string(m_cur_row, m_cur_col, str_id);
            }
            break;
            case cell_type_value:
            {
                // value cell
                double val = strtod(m_cur_value.get(), NULL);
                mp_sheet->set_value(m_cur_row, m_cur_col, val);
            }
            break;
            case cell_type_boolean:
            {
                // boolean cell
                bool val = strtoul(m_cur_value.get(), NULL, 10) != 0;
                mp_sheet->set_bool(m_cur_row, m_cur_col, val);
            }
            break;
            default:
                warn("unhanlded cell content type");
        }
    }

    if (m_cur_cell_xf)
        mp_sheet->set_format(m_cur_row, m_cur_col, m_cur_cell_xf);

    // reset cell related parameters.
    m_cur_value.clear();
    m_cur_formula_type.clear();
    m_cur_formula_ref.clear();
    m_cur_formula_str.clear();
    m_cur_shared_formula_id = -1;
}

}
