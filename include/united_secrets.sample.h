/*
 * united_secrets.sample.h -- copy to united_secrets.h (gitignored) and fill in. SWOS United's
 * secrets are never in git; the team shares them privately (SWOS-API-secrets.zip).
 *
 * The keys sensiblesoccer.de's API takes to create a ranked match (view=matchcreate,
 * action=createOOO / createOOOleg). A build without them cannot host a ranked match
 * (OPEN says so); it can still join one.
 */
#define UNITED_RANKED_CREATE_KEY ""
#define UNITED_RANKED_ADDLEG_KEY ""
