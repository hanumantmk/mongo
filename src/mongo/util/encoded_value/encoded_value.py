#    Copyright (C) 2014 MongoDB Inc.
#
#    This program is free software: you can redistribute it and/or  modify
#    it under the terms of the GNU Affero General Public License, version 3,
#    as published by the Free Software Foundation.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU Affero General Public License for more details.
#
#    You should have received a copy of the GNU Affero General Public License
#    along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
#    As a special exception, the copyright holders give permission to link the
#    code of portions of this program with the OpenSSL library under certain
#    conditions as described in each individual source file and distribute
#    linked combinations including the program with the OpenSSL library. You
#    must comply with the GNU Affero General Public License in all respects for
#    all of the code used other than as permitted herein. If you modify file(s)
#    with this exception, you may extend this exception to your version of the
#    file(s), but you are not obligated to do so. If you do not wish to do so,
#    delete this exception statement from your version. If you delete this
#    exception statement from all source files in the program, then also delete
#    it in the license file.

"""
encoded_value provides a number of helper classes that help form a meta
description of an encoded_value class.  When .cpp() is invoked on the object
heirarchy, a valid c++ class will be generated (bare of namespace and without
includes).

The form of that class intended to be:

template <convertEndian>
class NAME {
    static const int size = TOTAL_SIZE;
    typedef HIDEOUS_TEMPLATE_FOR_MUTABLE_POINTER Pointer;
    typedef HIDEOUS_TEMPLATE_FOR_CONST_POINTER CPointer;

    class Value {
        /* all of the methods with a char[] for storage */
    };

    class Reference {
        /* all of the methods with a char * for storage */
    };

    class CReference {
        /* all of the const methods with a const char * for storage */
    };
};

The basic idea is that you make a NAME<convertEndian>::Value if you want
owned memory, a ::Reference if you're pointing at some buffer that you want
to write to, and a ::CReference if you want a read-only value.

Variants are interchangable with the same endian strategy


On the generating side, the idea is that you'd convert something like:

#pragma pack(1)
struct foo {
    int x;
    short y;
    unsigned z0 : 2;
    unsigned z1 : 2;
    unsigned z2 : 4;
};

into

CLASS("foo", [
    FIELD("int", "x"),
    FIELD("short", "y"),
    BITFIELD("uint8_t", [
        FIELD("unsigned", "z0", 2)
        FIELD("unsigned", "z1", 2)
        FIELD("unsigned", "z2", 4)
    ]),
]).cpp()

Then use a wrapper python script to generate an includeable header

A full feature list includes:
  * inheritance (classes have a third param, which can be another
    encoded_value class).  Multiple inheritance is not allowed
  * unions.  See the UNION constructor
      * STRUCT facilitates this.  It provides logical nesting, rather than
        adding explicit subobjects.
  * SKIP to pad bytes
  * EVSTRUCT to embed other encoded_value classes.
  * EXTRA_CONST for extra methods that should be available everywhere
  * EXTRA_MUTABLE for extra methods only on the Reference and Value types
  * FIELD takes a third param which is context sensitive
      * in a CLASS, it represents an array of values
      * in a BITFIELD, it represents the number of bits in the value
"""

class CLASS:
    """ Takes a name, a list of fields and a possible parent to inherit from """

    def __init__(self, name, fields, parent = None):
        self.name = name
        self.fields = fields
        self.parent = parent

    def cpp(self):
        """ generates code for the class """

        fields = self.fields
        out = []

        # classes inherit the global endian conversion default
        out.extend(["template <enum encoded_value::endian::ConvertEndian convertEndian = encoded_value::endian::kDefault>\n"])
        out.extend(["class ", self.name, " {\n"])
        out.extend(["public:\n\n"])

        if self.parent:
            sizeof = [self.parent + "<>::size"]
        else:
            sizeof = ["0"]

        for field in fields:
            sizeof.append(field.sizeof())

        # size is the sum of the fields and the parent class.  This assumes
        # contigous storage for a char[] class inheriting from a char[] class.
        # That's probably true, but we should also static_assert. TODO
        out.extend(["    static const int size = ", ' + '.join(sizeof), ";\n\n"])

        out.extend(["    class Value;\n\n"])
        out.extend(["    class Reference;\n\n"])
        out.extend(["    class CReference;\n\n"])

        out.extend(["    typedef encoded_value::Impl::Pointer<encoded_value::Meta::EV<", self.name, "<convertEndian> >, Reference, char * > Pointer;\n\n"])
        out.extend(["    typedef encoded_value::Impl::Pointer<encoded_value::Meta::EV<", self.name, "<convertEndian> >, CReference, const char * > CPointer;\n\n"])

        offset = []

        # foreach field, calculate how many bytes into the shared buffer they
        # start based on the fields taht came before.
        for i in range(len(fields)):
            offset.append([sizeof[0]])

            for j in range(0, i):
                offset[i].append(fields[j].sizeof())

        # generate all of the subclasses
        self._cpp_helper(out, offset, sizeof, "Value");
        self._cpp_helper(out, offset, sizeof, "Reference");
        self._cpp_helper(out, offset, sizeof, "CReference");

        out.extend(["};"])

        return ''.join(out)

    def _cpp_helper(self, out, offset, sizeof, name):
        """ Generates the class subtypes (Value, Reference and CReference) """
        fields = self.fields

        if self.parent:
            out.extend(["class ", name, " : public ", self.parent, "<convertEndian>::", name, " {\n"])
        else:
            out.extend(["class ", name, " {\n"])

        out.extend(["public:\n"])

        out.extend(["    static const int size = ", ' + '.join(sizeof), ";\n\n"])

        # If we're a value type and have a parent, reserve some bytes for our
        # values on top of the char[] we're inheriting
        if self.parent and name == "Value":
            out.extend(["private:\n"])
            out.extend(["    char _ignore[size - ", self.parent, "<>::size];\n"])

        # Otherwise an array or other pointer type
        if not self.parent:
            out.extend(["protected:\n"])
            if name == "Value":
                out.extend(["    char storage[size];\n"])
            elif name == "CReference":
                out.extend(["    const char * storage;\n"])
            else:
                out.extend(["    char * storage;\n"])

        out.extend(["public:\n"])

        # These are a bunch of assignment methods.  So only for mutables
        if name != "CReference":
            out.extend(["    void zero() {\n"])
            out.extend(["        std::memset(this->storage, 0, size);\n"])
            out.extend(["    }\n\n"])

            out.extend(["    char * ptr() const {\n"])
            out.extend(["        return (char *)this->storage;\n"])
            out.extend(["    }\n\n"])

            out.extend(["    ", name, "& operator=(const Reference& p) {\n"])
            out.extend(["        std::memcpy(this->storage, p.ptr(), size);\n"])
            out.extend(["        return *this;\n"])
            out.extend(["    }\n\n"])

            out.extend(["    ", name, "& operator=(const CReference& p) {\n"])
            out.extend(["        std::memcpy(this->storage, p.ptr(), size);\n"])
            out.extend(["        return *this;\n"])
            out.extend(["    }\n\n"])

            out.extend(["    ", name, "& operator=(const Value& p) {\n"])
            out.extend(["        std::memcpy(this->storage, p.ptr(), size);\n"])
            out.extend(["        return *this;\n"])
            out.extend(["    }\n\n"])

            out.extend(["    Pointer operator &() {\n"])
            out.extend(["        return Pointer(this->storage);\n"])
            out.extend(["    }\n\n"])

        # everybody gets a const pointer operator&()
        out.extend(["    CPointer operator &() const {\n"])
        out.extend(["        return CPointer(this->storage);\n"])
        out.extend(["    }\n\n"])
        out.extend(["    ", name, "() {}\n\n"])


        # codegen for the fields
        for i in range(len(fields)):
            out.extend(fields[i].cpp(' + '.join(offset[i]), name == "CReference"))

        if name == "Value":
            out.extend(["    ", name, "(const char * in) {\n"])
            out.extend(["        std::memcpy(this->storage, in, size);\n"])
            out.extend(["    }\n\n"])

            out.extend(["    ", name, "(const Reference & p) {\n"])
            out.extend(["        std::memcpy(this->storage, p.ptr(), size);\n"])
            out.extend(["    }\n\n"])

            out.extend(["    ", name, "(const CReference & p) {\n"])
            out.extend(["        std::memcpy(this->storage, p.ptr(), size);\n"])
            out.extend(["    }\n\n"])

            out.extend(["    ", name, "(const Value& p) {\n"])
            out.extend(["        std::memcpy(this->storage, p.ptr(), size);\n"])
            out.extend(["    }\n\n"])

        elif name == "CReference":
            out.extend(["    ", name, "(const char * in) {\n"])
            out.extend(["        this->storage = in;\n"])
            out.extend(["    }\n\n"])

            out.extend(["    ", name, "(const Reference & p) {\n"])
            out.extend(["        this->storage = p.ptr();\n"])
            out.extend(["    }\n\n"])

            out.extend(["    ", name, "(const Value& p) {\n"])
            out.extend(["        this->storage = p.ptr();\n"])
            out.extend(["    }\n\n"])

        else:
            out.extend(["    ", name, "(char * in) {\n"])
            out.extend(["        this->storage = in;\n"])
            out.extend(["    }\n\n"])

            out.extend(["    ", name, "(const Value& p) {\n"])
            out.extend(["        this->storage = p.ptr();\n"])
            out.extend(["    }\n\n"])

        out.extend(["};\n\n"])

class FIELD:
    """ Generators accessors for regular fields """

    def __init__(self, t, name, array = None):
        self.type = t
        self.name = name
        self.array = array

    def sizeof(self):
        if self.array is None:
            return "sizeof(" + self.type + ")"
        else:
            return "(sizeof(" + self.type + ") * " + str(self.array) + ")"

    def cpp(self, offset_str, is_const):
        out = []

        # generating array versus regular accessors, then only producing
        # non-const methods on non-const object types
        if self.array is None:
            if not is_const:
                out.extend(["    encoded_value::Reference<", self.type, ", convertEndian> ", self.name, "() {\n"])
                out.extend(["        return encoded_value::Reference<", self.type, ", convertEndian>(this->storage +", offset_str, ");\n"])
                out.extend(["    }\n\n"])

            out.extend(["    encoded_value::CReference<", self.type, ", convertEndian> ", self.name, "() const {\n"])
            out.extend(["        return encoded_value::CReference<", self.type, ", convertEndian>(this->storage +", offset_str, ");\n"])
            out.extend(["    }\n\n"])
        else:
            if not is_const:
                out.extend(["    encoded_value::Pointer<", self.type, ", convertEndian> ", self.name, "() {\n"])
                out.extend(["        return encoded_value::Pointer<", self.type, ", convertEndian>(this->storage +", offset_str, ");\n"])
                out.extend(["    }\n\n"])

            out.extend(["    encoded_value::CPointer<", self.type, ", convertEndian> ", self.name, "() const {\n"])
            out.extend(["        return encoded_value::CPointer<", self.type, ", convertEndian>(this->storage +", offset_str, ");\n"])
            out.extend(["    }\n\n"])

        return out

class SKIP:
    """ Just skip N bytes in whatever context """
    def __init__(self, skip):
        self.skip = skip

    def sizeof(self):
        return str(self.skip)

    def cpp(self, offset_str, is_const):
        return []

class BITFIELD:
    """ BitField generates bitfield accessors.  It doesn't call down
        recursively, but instead uses the data contained FIELD's hold
    """

    def __init__(self, root, fields):
        self.fields = fields
        self.root = root

    def sizeof(self):
        return "sizeof(" + self.root + ")"

    def cpp(self, offset_str, is_const):
        out = []
        offset = 0

        for field in self.fields:
            if (isinstance(field, SKIP)):
                offset += field.skip
            else:
                bitfield_impl = field.type + ", " + self.root + ", " + str(offset) + ", " + str(field.array) + ", convertEndian"

                if not is_const:
                    out.extend(["    encoded_value::BitField::Reference<", bitfield_impl, "> ", field.name, "() {\n"])
                    out.extend(["        return encoded_value::BitField::Reference<", bitfield_impl, ">(this->storage +", offset_str, ");\n"])
                    out.extend(["    }\n\n"])

                out.extend(["    encoded_value::BitField::CReference<", bitfield_impl, "> ", field.name, "() const {\n"])
                out.extend(["        return encoded_value::BitField::CReference<", bitfield_impl, ">(this->storage +", offset_str, ");\n"])
                out.extend(["    }\n\n"])

                offset += field.array

        return out

class UNION:
    """ UNION generates accessors that point to the same locations in memory.
    It's useful, combined with STRUCT, to provide type'd variant types """

    def __init__(self, fields):
        self.fields = fields

    def sizeof(self):
        out = []
        for i in range(len(self.fields) - 1):
            out.extend([ "encoded_value::_max< ", self.fields[i].sizeof(), ", "])

        out.append( self.fields[len(self.fields) - 1].sizeof() )

        for i in range(len(self.fields) - 1):
            out.append( ">::result " )

        return ''.join(out)

    def cpp(self, offset_str, is_const):
        out = []

        for field in self.fields:
            out.extend(field.cpp(offset_str, is_const))

        return out

class STRUCT:
    """ Mostly useful with UNION """

    def __init__(self, fields):
        self.fields = fields

    def sizeof(self):
        out = []
        for field in self.fields:
            out.append(field.sizeof())

        return ' + '.join(out)

    def cpp(self, offset_str, is_const):
        fields = self.fields

        out = []

        offset = []

        for i in range(len(fields)):
            offset.append([offset_str])

            for j in range(0, i):
                offset[i].append(fields[j].sizeof())

        for i in range(len(fields)):
            out.extend(fields[i].cpp(' + '.join(offset[i]), is_const))

        return out

class EVSTRUCT:
    """ Provides access to embedded encoded_value objects """

    def __init__(self, t, name, array = None):
        self.type = t
        self.name = name
        self.array = array

    def sizeof(self):
        if self.array is None:
            return self.type + "<convertEndian>::size"
        else:
            return "(" + self.type + "<convertEndian>::size * " + str(self.array) + ")"

    def cpp(self, offset_str, is_const):
        out = []

        # generating array versus regular accessors, then only producing
        # non-const methods on non-const object types
        if self.array is None:
            if not is_const:
                out.extend(["    ", self.type, "<convertEndian>::Reference ", self.name, "() {\n"])
                out.extend(["        return ", self.type, "<convertEndian>::Reference(this->storage +", offset_str, ");\n"])
                out.extend(["    }\n\n"])

            out.extend(["    ", self.type, "<convertEndian>::CReference ", self.name, "() const {\n"])
            out.extend(["        return ", self.type, "<convertEndian>::CReference(this->storage +", offset_str, ");\n"])
            out.extend(["    }\n\n"])
        else:
            if not is_const:
                out.extend(["    ", self.type, "<convertEndian>::Pointer ", self.name, "() {\n"])
                out.extend(["        return ", self.type, "<convertEndian>::Pointer(this->storage +", offset_str, ");\n"])
                out.extend(["    }\n\n"])

            out.extend(["    ", self.type, "<convertEndian>::CPointer ", self.name, "() const {\n"])
            out.extend(["        return ", self.type, "<convertEndian>::CPointer(this->storage +", offset_str, ");\n"])
            out.extend(["    }\n\n"])

        return out
