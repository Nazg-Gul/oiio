/////////////////////////////////////////////////////////////////////////////
// Copyright 2004 NVIDIA Corporation and Copyright 2008 Larry Gritz.
// All Rights Reserved.
//
// Extensions by Larry Gritz based on open-source code by NVIDIA.
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
// 
// * Redistributions of source code must retain the above copyright
//   notice, this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright
//   notice, this list of conditions and the following disclaimer in the
//   documentation and/or other materials provided with the distribution.
// * Neither the name of NVIDIA nor the names of its contributors
//   may be used to endorse or promote products derived from this software
//   without specific prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// (This is the Modified BSD License)
/////////////////////////////////////////////////////////////////////////////


#include <cstring>
#include <cctype>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <cstdarg>
#include <iterator>

#include "strutil.h"

#define DLL_EXPORT_PUBLIC /* Because we are implementing ArgParse */
#include "argparse.h"
#undef DLL_EXPORT_PUBLIC



// Constructor.  Does not do any parsing or error checking.
// Make sure to call initialize() right after construction.
ArgOption::ArgOption (const char *str) 
    : format(str), type(None), count(0), callback(NULL), repetitions(0)
{
}



// Parses the format string ("-option %s %d %f") to extract the
// flag ("-option") and create a code string ("sdf").  After the
// code string is created, the param list of void* pointers is
// allocated to hold the argument variable pointers.
int
ArgOption::initialize()
{
    size_t n;
    const char *s;

    if (format.empty() || format == "%*") {
        type = Sublist;
        count = 1;                      // sublist callback function pointer
        code = "*";
        flag = "";
    } else {
        // extract the flag name
        s = &format[0];
        assert(*s == '-');
        assert(isalpha(s[1]));
    
        s++;

        while (isalnum(*s) || *s == '_' || *s == '-') s++;

        n = s - (&format[0]) + 1;
        flag.assign (format.begin(), format.begin()+n);

        // Check to see if this is a simple flag option
        if (n == format.length() + 1) {
            type = Flag;
            count = 1;
            code = "!";
        } else {
            // Parse the scanf-like parameters

            type = Regular;
    
            n = (format.length() - n) / 2;       // conservative estimate
            code.clear ();
    
            while (*s != '\0') {
                if (*s == '%') {
                    s++;
                    assert(*s != '\0');
            
                    count++;                    // adding another parameter
            
                    switch (*s) {
                    case 'd':                   // 32bit int
                    case 'g':                   // float
                    case 'f':                   // float
                    case 'F':                   // double
                    case 's':                   // char * 
                    case 'S':                   // allocated char *
                    case 'L':                   // allocated char * list
                        assert (type == Regular);
                        code += *s;
                        break;

                    case '*':
                        assert(count == 1);
                        type = Sublist;
                        break;
                        
                    default:
                        std::cerr << "Programmer error:  Unknown option ";
                        std::cerr << "type string \"" << *s << "\"" << "\n";
                        abort();
                    }
                }
        
                s++;
            }
        }
    }
    
    // Allocate space for the parameter pointers and initialize to NULL
    param.resize (count, NULL);

    return 0;
}



// Stores the pointer to an argument in the param list and
// initializes flag options to FALSE.
// FIXME -- there is no such initialization.  Bug?
void
ArgOption::add_parameter (int i, void *p)
{
    assert (i >= 0 && i < count);
    param[i] = p;
}



// Given a string from argv, set the associated option parameter
// at index i using the format conversion code in the code string.
void
ArgOption::set_parameter (int i, const char *argv)
{
    assert(i < count);
    
    switch (code[i]) {
    case 'd':
        *(int *)param[i] = atoi(argv);
        break;

    case 'f':
    case 'g':
        *(float *)param[i] = (float)atof(argv);
        break;

    case 'F':
        *(double *)param[i] = atof(argv);
        break;

    case 's':
        *(std::string *)param[i] = argv;
        break;

    case 'S':
        *(std::string *)param[i] = argv;
        break;

    case 'L':
        if (! (*(std::string *)param[i]).empty())
            *(std::string *)param[i] += " ";
        *(std::string *)param[i] += argv;
        break;

    case '!':
        *(bool *)param[i] = true;
        break;
        
    case '*':
    default:
        abort();
    }
}



// Call the sublist callback if any arguments have been parsed
int
ArgOption::invoke_callback () const
{
    assert (count == 1);

    int argc = (int) argv.size();
    if (argc == 0)
        return 0;

    // Convert the argv's to char*[]
    const char **myargv = (const char **) alloca (argc * sizeof(const char *));
    for (int i = 0;  i < argc;  ++i)
        myargv[i] = argv[i].c_str();
    if (((int (*)(int, const char **))param[0]) (argc, myargv) < 0) {
        return -1;
    }

    return 0;
}



// Add an argument to this sublist option
void
ArgOption::add_argument (char *argv)
{
    this->argv.push_back (argv);
}





ArgParse::ArgParse (int argc, const char **argv)
    : argc(argc), argv((char **)argv), global(NULL)
{
}



// Called after all command line parsing is completed, this function
// will invoke all callbacks for sublist arguments.  
inline int
ArgParse::invoke_all_sublist_callbacks()
{
    for (std::vector<ArgOption *>::const_iterator i = option.begin();
         i != option.end(); i++) {
        
        if (!(*i)->is_sublist()) {
            continue;
        }
        
        if ((*i)->invoke_callback() < 0) {
            return -1;
        }
    }

    return 0;
}



// Top level command line parsing function called after all options
// have been parsed and created from the format strings.  This function
// parses the command line (argc,argv) stored internally in the constructor.
// Each command line argument is parsed and checked to see if it matches an
// existing option.  If there is no match, and error is reported and the
// function returns early.  If there is a match, all the arguments for
// that option are parsed and the associated variables are set.
inline int
ArgParse::parse_command_line()
{
    for (int i = 1; i < argc; i++) {

        if (argv[i][0] == '-' && 
              (isalpha (argv[i][1]) || argv[i][1] == '-')) {         // flag

            ArgOption *option = find_option (argv[i]);
            if (option == NULL) {
                error ("Invalid option \"%s\"", argv[i]);
                return -1;
            }

            option->found_on_command_line();
            
            if (option->is_flag()) {
                option->set_parameter(0, NULL);
                if (global != NULL && global->name()[0] != '\0')
                    global = NULL;                              // disable
            } else if (option->is_sublist()) {
                global = option;                                // reset global
            } else {
                assert (option->is_regular());
                if (global != NULL && global->name()[0] != '\0')
                    global = NULL;                              // disable
                
                for (int j = 0; j < option->parameter_count(); j++) {

                    if (j+i+1 >= argc) {
                        error ("Missing parameter %d from option "
                                      "\"%s\"", j+1, option->name().c_str());
                        return -1;
                    }

                    option->set_parameter (j, argv[i+j+1]);
                }

                i += option->parameter_count();
            }
            
        } else {
            // not an option nor an option parameter, glob onto global list
            if (global == NULL) {
                error ("Argument \"%s\" does not have an associated "
                    "option", argv[i]);
                return -1;
            }

            global->add_argument (argv[i]);
        }
    }

    return 0;
}



// Primary entry point.  This function accepts a set of format strings
// and variable pointers.  Each string contains an option name and a
// scanf-like format string to enumerate the arguments of that option
// (eg. "-option %d %f %s").  The format string is followed by a list
// of pointers to the argument variables, just like scanf.  All format
// strings and arguments are parsed to create a list of ArgOption objects.
// After all ArgOptions are created, the command line is parsed and
// the sublist option callbacks are invoked.
int
ArgParse::parse (const char *format, ...)
{
    va_list ap;
    va_start (ap, format);

    for (const char *cur = format; cur != NULL; cur = va_arg (ap, char *)) {

        if (find_option (cur)) {
            error ("Option \"%s\" is multiply defined");
            return -1;
        }
        
        // Build a new option and then parse the values
        ArgOption *option = new ArgOption (cur);
        if (option->initialize() < 0) {
            return -1;
        }

        if (cur[0] == '\0' ||
            (cur[0] == '%' && cur[1] == '*' && cur[2] == '\0')) {
            // set default global option
            global = option;
        }
        
        // Grab any parameters and store them with this option
        for (int i = 0; i < option->parameter_count(); i++) {

            void *p = va_arg (ap, void *);
            if (p == NULL) {
                error ("Missing argument parameter for \"%s\"",
                              option->name().c_str());
                return -1;
            }
            
            option->add_parameter (i, p);
        }

        this->option.push_back(option);
    }

    va_end (ap);

    if (parse_command_line() < 0) {
        return -1;
    }

    if (invoke_all_sublist_callbacks() < 0) {
        return -1;
    }
    
    return 0;
}



// Find an option by name in the option vector
ArgOption *
ArgParse::find_option(const char *name)
{
    for (std::vector<ArgOption *>::const_iterator i = option.begin();
         i != option.end(); i++) {
        if (! strcmp(name, (*i)->name().c_str()))
            return *i;
    }

    return NULL;
}



int
ArgParse::found(char *option_name)
{
    ArgOption *option = find_option(option_name);
    if (option == NULL) return 0;
    return option->parsed_count();
}



void
ArgParse::error (const char *format, ...)
{
    va_list ap;
    va_start (ap, format);
    errmessage = Strutil::vformat (format, ap);
    va_end (ap);
}



ArgParse::~ArgParse()
{
    for (std::vector<ArgOption *>::const_iterator i = option.begin();
         i != option.end(); i++) {
        delete *i;
    }
}
