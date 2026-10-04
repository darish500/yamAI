#ifndef YAM_TYPES_H
#define YAM_TYPES_H

// =========================================================================
// DIAGNOSIS TYPES
// -------------------------------------------------------------------------
// Pulled into its own header (rather than defined inline in the .ino) so
// it's fully defined before the Arduino IDE auto-generates function
// prototypes. If these types lived directly in the .ino, the IDE inserts
// forward declarations for diagnoseYam()/applyRelayLogic() right after the
// #include block — before the struct/enum definitions further down the
// file — which fails to compile ("YamDiagnosis does not name a type").
// =========================================================================

enum DiseaseId {
  DISEASE_NONE = 0,
  DISEASE_DRY_ROT,          // e.g. Fusarium spp. - moderate temp/RH, wounds
  DISEASE_WET_SOFT_ROT,     // bacterial/Rhizopus - very high RH + strong off-gassing
  DISEASE_COUNT
};
static const char* DISEASE_NAMES[DISEASE_COUNT] = {
  "None", "Possible_Dry_Rot", "Possible_Wet_Soft_Rot"
};

enum PestId {
  PEST_NONE = 0,
  PEST_MEALYBUG,            // warm + humid, poor ventilation
  PEST_WEEVIL_BEETLE,       // warm + comparatively drier, bores into tuber
  PEST_COUNT
};
static const char* PEST_NAMES[PEST_COUNT] = {
  "None", "Possible_Mealybug", "Possible_Weevil_or_Beetle"
};

enum ThreatLevel {
  THREAT_NONE = 0,
  THREAT_LOW,
  THREAT_MID,
  THREAT_HIGH,
  THREAT_COUNT
};
static const char* THREAT_NAMES[THREAT_COUNT] = {
  "None", "Low", "Mid", "High"
};

struct YamDiagnosis {
  int diseaseValue;   // index into DISEASE_NAMES
  int pestValue;      // index into PEST_NAMES
  int threatLevel;    // index into THREAT_NAMES
};

#endif
