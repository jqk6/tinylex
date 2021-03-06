#include <stdio.h>
#include <stdlib.h>
#include <lib.h>
#include <dfa.h>
#include <nfa.h>
#include <set.h>

struct set *groups[MAX_GROUPS];
int ngroups;
int sgroup;	/* start group */

/* group auxiliary functio n*/
void add_group(int group, int dfa)
{
	dfastates[dfa].group = group;
	addset(groups[group], dfa);
}

void del_group(int dfa)
{
	if (dfastates[dfa].group < 0)
		errexit("del dfa not in some group");
	delset(groups[dfastates[dfa].group], dfa);
	dfastates[dfa].group = F;
}

int new_group(void)
{
	if (ngroups >= MAX_GROUPS)
		errexit("groups overflow!");
	groups[ngroups] = newset();
	return ngroups++;
}

void debug_group(void);
int init_groups(void)
{
	int i, k, group;
	struct dfa *dfa;
	struct set *nonaccept;

	for (i = 0; i < MAX_GROUPS; i++)
		groups[i] = NULL;
	/* init accept group and non-accept group */
	sgroup = 0;
	ngroups = 0;
	new_group();		/* for non-accept group */
	for (dfa = dfastates, i = 0; i < ndfas; dfa++, i++) {
		group = 0;
		if (dfa->accept) {
			/* find the same accept(action) group */
			for (k = 0; k < i; k++) {
				if (dfastates[k].accept &&
				dfastates[k].accept->action == dfa->accept->action) {
					group = dfastates[k].group;
					break;
				}
			}
			/* not found, we create new accept group */
			if (group == 0)	/* group 0 is non-accept group */
				group = new_group();
		}
		add_group(group, i);
	}

	/* fix: Is non-accept group? Remove it! */
	if (emptyset(groups[0])) {
		freeset(groups[0]);
		groups[0] = NULL;
		sgroup = 1;
	}
#ifdef DEBUG
	debug_group();
#endif
}

/* compute dfastates[dfatable[dfa][c]].group */
int transgroup(int (*dfatable)[128], int dfa, int c)
{
	int nextdfa;
	nextdfa = dfatable[dfa][c];
	if (nextdfa == F)
		return F;
	/* dfastates[].group != F */
	return dfastates[nextdfa].group;
}

void part_groups(int (*dfatable)[128], int g, int c)
{
	struct set *group;
	int firstgrp, newgrp;
	int dfa, nextdfa;

	/* get group set */
	group = groups[g];
	newgrp = -1;
	/* get first dfa group */
	startmember(group);
	dfa = nextmember(group);
	/* empty group */
	if (dfa == -1)
		return;
	firstgrp = nextdfa = transgroup(dfatable, dfa, c);
	/* loop every dfa in group */
	while ((dfa = nextmember(group)) != -1) {
		/* get dfa -- on c --> nextdfa::group */
		if (transgroup(dfatable, dfa, c) != firstgrp) {
			if (newgrp < 0)
				newgrp = new_group();
			del_group(dfa);
			add_group(newgrp, dfa);
		}
	}
}

/*
 * reset dfa accept string,
 * make it associated its new state(group) number
 * delete orignal accept set
 */
void reset_accept(struct set *accept, struct set **ap)
{
	/* temp accept string stack */
	struct {
		int group;
		struct accept *accept;
	} accept_stack[MAX_GROUPS];

	int i, top = 0;
	struct dfa *dfa;
	struct set *new;

	/* new accept state set */
	new = newset();
	/* reset accept state */
	for_each_member(i, accept) {
		dfa = &dfastates[i];
		if (!dfa->accept)
			errexit("accept state dfa has no accept structure");
		/* save accept string */
		accept_stack[top].group = dfa->group;
		accept_stack[top].accept = dfa->accept;
		dfa->accept = NULL;
		top++;
		/* new accept set */
		addset(new, dfa->group);
	}
	/* check */
	for (i = 0; i < ndfas; i++)
		if (dfastates[i].accept)
			errexit("reset accept state error");
	/* restore saved accept */
	for (i = 0; i < top; i++) {
		/* reduplicate accept dfa? */
		if (dfastates[accept_stack[i].group].accept) {
			freeaccept(accept_stack[i].accept);
		} else {
			dfastates[accept_stack[i].group].accept =
					accept_stack[i].accept;
		}
	}

	if (ap)
		*ap = new;
	else
		freeset(new);
	freeset(accept);
}

void minimize_dfa(int (*table)[128], struct set *accept, struct set **ap)
{
	int c, group, n, start_group;

	init_groups();
	/* minimize dfatable */
	do {
		/* backup ngroups */
		n = ngroups;
		/* loop MAX_CHARS and loop ngroups can swich order */
		for (c = 0; c < MAX_CHARS; c++) {	/* loop MAX_CHARS */
			for (group = sgroup; group < n; group++) {/* loop ngroups*/
				part_groups(table, group, c);
			}
		}
	} while (n < ngroups);	/* add a new group(n < ngroups)? */

	reset_accept(accept, ap);
}

int minimize_dfatable(int (*table)[128], int (**ret)[128])
{
	struct set *group;
	int (*t)[128];		/* new table */
	int g, dfa, c;
#ifdef DEBUG_MIN_TABLE
	int times = 0;
#endif
	/* alloc new minimized table */
	t = xmalloc(MAX_CHARS * (ngroups - sgroup) * sizeof(int));

	group = newset();
	/*
	 * loop hierarchy:
	 *   DFAS --> Groups --> Chars
	 */
	for (dfa = 0; dfa < ndfas; dfa++) {
		/*
		 * FIXED: Different dfa can make the same g!
		 *        How to reduce the same g times?
		 * use group to detect whether group g has been handled!
		 */
		g = dfastates[dfa].group;
		if (memberofset(g, group))
			continue;
		addset(group, g);
#ifdef DEBUG_MIN_TABLE
		times++;
#endif
		for (c = 0; c < MAX_CHARS; c++)
			if (table[dfa][c] == F)
				t[g - sgroup][c] = F;
			else
				t[g - sgroup][c] = dfastates[table[dfa][c]].group - sgroup;
	}
	/* set return table */
	if (ret)
		*ret = t;
#ifdef DEBUG_MIN_TABLE
	dbg(" %d * 128(chars) times", times);
#endif
	return (ngroups - sgroup);
}

int minimize_dfatable2(int (*table)[128], int (**ret)[128])
{
	struct set *group;
	int (*t)[128];		/* new table */
	int g, dfa, c;

#ifdef DEBUG_MIN_TABLE
	int times = 0;
#endif
	/* alloc new minimized table */
	t = xmalloc(MAX_CHARS * (ngroups - sgroup) * sizeof(int));
	/*
	 * loop hierarchy:
	 *   Groups --> DFAs --> Chars
	 *
	 * NOTE: new table row start from 0, not sgroup
	 */
	for (g = sgroup; g < ngroups; g++) {
		startmember(groups[g]);
		if ((dfa = nextmember(groups[g])) != -1) {
#ifdef DEBUG_MIN_TABLE
			times++;
#endif
			for (c = 0; c < MAX_CHARS; c++)
				if (table[dfa][c] == F)
					t[g - sgroup][c] = F;
				else
					t[g - sgroup][c] = dfastates[table[dfa][c]].group - sgroup;
			/* only one table */
		} else {
			errexit("Group is empty!");
		}
	}
	if (ret)
		*ret = t;
#ifdef DEBUG_MIN_TABLE
	dbg(" %d * 128(chars) times", times);
#endif
	return (ngroups - sgroup);
}

void debug_group(void)
{
	int i, g, n;
	fprintf(stderr, "\n-------debug group-----------\n");
	fprintf(stderr, "\n[dfas]\n");
	for (i = 0; i < ndfas; i++) {
		g = dfastates[i].group;
		if (g == -1)
			continue;
		fprintf(stderr, "dfa %d group %d ", i, g);
		if (dfastates[i].accept)
			fprintf(stderr, "accept %s\n", dfastates[i].accept->action);
		else
			fprintf(stderr, "no accept\n");
	}
	fprintf(stderr, "[groups]\n");
	for (g = sgroup; g < ngroups; g++) {
		fprintf(stderr, "group:%d dfas:", g);
		for_each_member(n, groups[g])
			fprintf(stderr, "%d ", n);
		fprintf(stderr, "\n");
	}
	fprintf(stderr, "-------end debug group-------\n\n");
}

#ifdef MIN_DFA_TEST

int main(int argc, char **argv)
{
	struct nfa *nfa;
	struct set *accept;
	struct set *ap;

	int (*table)[MAX_CHARS];	/* dfa table */
	int (*mintable)[MAX_CHARS];	/* minimized dfa table */
	int (*mintable2)[MAX_CHARS];	/* minimized dfa table2 */
	int size;	/* dfa table size */

	if (argc != 2)
		errexit("ARGC != 2");

	/* init token stream: interpreting regular expression */
	text_open(argv[1]);
	/* construct NFA from regular expression */
	init_nfa_buffer();
	nfa = machine();
	traverse_nfa(nfa);

	/* construct dfa table */
	size = construct_dfa(nfa, &table, &accept);
	traverse_dfatable(table, size, accept);
	free_nfas();

	/* minimization */
	minimize_dfa(table, accept, &ap);

	/* debug */
	debug_group();

	dbg("------minimization 1 test--------");
	minimize_dfatable(table, &mintable);
	traverse_dfatable(mintable, ngroups - sgroup, ap);

	dbg("------minimization 2 test--------");
	minimize_dfatable2(table, &mintable2);
	traverse_dfatable(mintable2, ngroups - sgroup, ap);

	return 0;
}

#endif
