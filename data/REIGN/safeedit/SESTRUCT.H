//
// SafeStructs.h
//
// SafeEdit text editor configuration structures for Dark Reign FSMs.
//
// Definitions and types used in SafeText configuration,



// Maximumum number of Safe Elements for a configuration
#define MAX_SAFE_ELEMENTS 300

///////////////////////////////////////////////////////////////////////////////
//
// Safe Elements List
// -------------------
// This structure holds a list of names for the safe elements
struct SAFE_ELEMENTS_NAME_LIST
{
	char element_name[80];				// The name of the safe element
};

// How many subelements can we have in an element?
#define MAX_SAFE_SUBELEMENTS 100

///////////////////////////////////////////////////////////////////////////////
//
// Safe Element
// --------------------
//
// A single safe element.  It consists of a number of subelements, which can
// be atomic elements such as integers, editable text, or safe text (to name a
// few), and user defined elements.
//
struct SAFE_ELEMENT 
{
  char indentor[50];                  // String to separate next field
  char display_name[300];             // What to show when subelement not filled in
  char ascii_type[80];                // What type of element is subelement?
  int  min_count;                     // How many of these can we have?
  int  max_count;
  int  lower_bounded;                 // Is there a lower bound?
  int  lower_bound;                   // Lower bound
  int  upper_bounded;                 // Is there a upper bound?
  int  upper_bound;                   // Upper bound
  char separator[50];                 // String to separate next field
  int  indent;                        // Is this element to be indented?
  int  extra_indentation;             // Amount more that children should be indented
  char terminator[50];                // What ends this in parsing?
};

#define UNBOUNDED 0
#define BOUNDED 1
#define NA -1
#define UNLIMITED -999

#define INDENT 1
#define DONT_INDENT 0

// Parsing aid constants
#define NEEDED 0
#define TERMINATOR 1
#define OPTIONAL 2

