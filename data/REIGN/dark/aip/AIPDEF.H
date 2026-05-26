///////////////////////////////////////////////////////////////////////////////
// UCP Data Structure
// -------------------
// Default Unit Construction Program Definition.
// When no other task demands a Construction Program, build according
// to this program.
struct UNIT_CONSTRUCTION_PROGRAM
{
	char account_name[80];				// The names of the budgetary accounts
	int budget;										// How much money gets fed into account
  int max_reserve;
};

#define UNLIMITED -1

#define YES 1
#define NO 0

// Targetting types: Low is simple, high is better
#define TACTAI_LOW 0
#define TACTAI_HIGH 2

#define MAX_UCP_LENGTH 20

///////////////////////////////////////////////////////////////////////////////
// ACCOUNT Data Structure
// -------------------
// Default Unit Construction Program Definition.
// When no other task demands a Construction Program, build according
// to this program.
struct ACCOUNT
{
	int priority;									// What level of priority does this element have
	char item_name[80];						// What sort of thing should we buy/build?
	int build_type;								// Do we want fixed amounts, ratios, or what?
	int build_amount;							// How much of something (or what ratio) do we want.
};

#define NUMBER_TO_HAVE 0
#define NUMBER_TO_BUILD 1
#define RATIO_TO_BUILD 2
#define RATIO_TO_HAVE 3

#define MAX_ACCOUNT_LENGTH 50


///////////////////////////////////////////////////////////////////////////////
// Force Matching Data Structure
// -------------------
// Unit Force matching modification definition.
// All units in the game have a calculated unit strength which is roughly
// (Damage_Dealt * Hitpoints)/Rate_of_fire (there is a minimum strength, too)
// this allows the designer to assign coefficients to the strengths of particular
// units (such as "Tachyon Tank" 2.0, which would double the apparent strength).
struct FORCE_MATCHING
{
	char unit_name[80];				// The name of the unit type
  float multiplier;         // How much by which to multiply its strength
};

#define MAX_FORCE_MATCHING 100


/////////////////////////////////////////////////////////////////////////////
// BUILDING MATCHING
// --------------\n"
// Which buildings do we want to emphasize & target with this aip?
// Note that this can be used for BOTH the AI team's unit strengths and
// the opponents'
// Where would we want to build the buildings if we could build them?
/////////////////////////////////////////////////////////////////////////////
struct BUILDING_MATCHING
{
	char unit_name[80];				// The name of the unit type
  float multiplier;         // How much by which to multiply its strength
  int  build_pos;           // How much by which to multiply its strength
};

#define MAX_BUILDINGS 100

#define DONT_CARE 0
#define CENTER_OF_BASE 1
#define PERIMETER 2
#define OUTSIDE_OF_BASE 3
#define NEAR_ENEMY_BASE 4
#define NEAR_ENEMY_TROOPS 5


/////////////////////////////////////////////////////////////////////////////
// UNIT_MATCH_UP
// -------------
// When we see a unit of one of the following types, build the corresponding 
// unit in the corresponding quantity
/////////////////////////////////////////////////////////////////////////////
struct UNIT_MATCH_UP
{
	char unit_name[80];				    // The name of the unit type
	char match_up_name[80];				// The name of the unit type
  float match_amount;           // How much by which to multiply its strength
};


