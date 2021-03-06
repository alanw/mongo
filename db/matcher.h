// matcher.h

/* JSMatcher is our boolean expression evaluator for "where" clauses */

/**
*    Copyright (C) 2008 10gen Inc.
*
*    This program is free software: you can redistribute it and/or  modify
*    it under the terms of the GNU Affero General Public License, version 3,
*    as published by the Free Software Foundation.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*
*    You should have received a copy of the GNU Affero General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include "jsobj.h"
#include <pcrecpp.h>

namespace mongo {

    class RegexMatcher {
    public:
        const char *fieldName;
        pcrecpp::RE *re;
        RegexMatcher() {
            re = 0;
        }
        ~RegexMatcher() {
            delete re;
        }
    };

    class BasicMatcher {
    public:
        BSONElement toMatch;
        int compareOp;
    };

// SQL where clause equivalent
    class Where;
    class DiskLoc;

    /* Match BSON objects against a query pattern.

       e.g.
           db.foo.find( { a : 3 } );

       { a : 3 } is the pattern object.

       GT/LT:
       { a : { $gt : 3 } }

       Not equal:
       { a : { $ne : 3 } }

       TODO: we should rewrite the matcher to be more an AST style.
    */
    class JSMatcher : boost::noncopyable {
        int matchesDotted(
            const char *fieldName,
            const BSONElement& toMatch, const BSONObj& obj,
            int compareOp, bool *deep, bool isArr = false);

        int matchesNe(
            const char *fieldName,
            const BSONElement &toMatch, const BSONObj &obj,
            bool *deep);
        
        struct element_lt
        {
            bool operator()(const BSONElement& l, const BSONElement& r) const
            {
                int x = (int) l.type() - (int) r.type();
                if ( x < 0 ) return true;
                if ( x > 0 ) return false;
                return compareElementValues(l,r) < 0;
            }
        };
    public:
        static int opDirection(int op) {
            return op <= BSONObj::LTE ? -1 : 1;
        }

        // Only specify constrainIndexKey if matches() will be called with
        // index keys having empty string field names.
        JSMatcher(const BSONObj &pattern, const BSONObj &constrainIndexKey = BSONObj());

        ~JSMatcher();

        /* deep - means we looked into arrays for a match
        */
        bool matches(const BSONObj& j, bool *deep = 0);
        
        bool keyMatch() const { return !all && !haveSize; }
    private:
        void addBasic(const BSONElement &e, int c) {
            // TODO May want to selectively ignore these element types based on op type.
            if ( e.type() == MinKey || e.type() == MaxKey )
                return;
            BasicMatcher bm;
            bm.toMatch = e;
            bm.compareOp = c;
            basics.push_back(bm);
        }

        int valuesMatch(const BSONElement& l, const BSONElement& r, int op, bool *deep=0);

        set<BSONElement,element_lt> *in; // set if query uses $in
        set<BSONElement,element_lt> *nin; // set if query uses $nin
        set<BSONElement,element_lt> *all; // set if query uses $all
        Where *where;                    // set if query uses $where
        BSONObj jsobj;                  // the query pattern.  e.g., { name: "joe" }
        BSONObj constrainIndexKey_;
        
        vector<BasicMatcher> basics;
//        int n;                           // # of basicmatcher items
        bool haveSize;

        RegexMatcher regexs[4];
        int nRegex;

        // so we delete the mem when we're done:
        vector< shared_ptr< BSONObjBuilder > > builders_;
    };
    
    // If match succeeds on index key, then attempt to match full record.
    class KeyValJSMatcher : boost::noncopyable {
    public:
        KeyValJSMatcher(const BSONObj &pattern, const BSONObj &indexKeyPattern);
        bool matches(const BSONObj &j, bool *deep = 0);
        bool matches(const BSONObj &key, const DiskLoc &recLoc, bool *deep = 0);
    private:
        JSMatcher keyMatcher_;
        JSMatcher recordMatcher_;
    };

} // namespace mongo
