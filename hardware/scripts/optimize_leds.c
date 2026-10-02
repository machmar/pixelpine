/* PixelPine LED arrangement optimiser (simulated annealing), driven by optimize_leds.py.
 *
 * State per LED position n: the charlieplex pair (a = anode line, c = cathode line) and an orientation flag f
 * (f = 0: anode on the up-right pad of the 45 degree LED, f = 1: LED soldered turned round by 180 degrees).
 *
 * Why: the 480 LED pads sit on a "brick" lattice with only ~0.55 mm between neighbouring pads. Two neighbouring pads
 * on the same line net can be joined by a tiny F.Cu bridge, so a net becomes a few copper islands and only the links
 * between islands need inner-layer wiring. The cost is, per line net, a minimum spanning tree over its pads and its
 * line resistor in which a bridge costs ADJ_W and every other link costs length + VIA_PEN (mm).
 *
 * usage: optimize_leds <positions.txt> <iterations> <seed> <out.txt> [-flip] [-T0 t] [-T1 t] [-adj w] [-via p]
 *                      [-init start.txt]
 *   positions.txt: N, then N lines "x y" (LED centres, mm, y down). out.txt: N lines "a c f".
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#define N 240
#define L 16
#define NP (2 * N)
#define NT (NP + L)

static double px[NT], py[NT], W[NT][NT];
static int pa[N], pc[N], pf[N];       // pair + orientation at each position
static int nl[L][40], nc[L];
static double netcost[L];
static double ADJ_W = 0.5, VIA_PEN = 1.5, LEN_EXP = 1.0;
static int ALLOW_FLIP = 0;

static uint64_t rs;
static inline uint64_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs; }
static inline double urand(void) { return (rnd() >> 11) * (1.0 / 9007199254740992.0); }
static inline int irand(int n) { return (int)(rnd() % (uint64_t)n); }

static inline int padnet(int pad) {
    int n = pad >> 1, isK = pad & 1;
    int anode_on_A = !pf[n];
    // pad A (even) carries the anode when f == 0
    if (!isK) return anode_on_A ? pa[n] : pc[n];
    return anode_on_A ? pc[n] : pa[n];
}

static int adjn[NP][6], nadj[NP], pnet[NP], pidx[NP];
static int near_cnt[N], near_list[N][N];

static double mst_cost(int net, int *n_adj_out, int *n_long_out) {
    /* Kruskal on the cheap adjacency edges first (they are all lighter than any inner-layer link), then Prim over
       the resulting copper islands using the closest pad pair between islands. */
    int m = nc[net], t[40], comp[40];
    for (int i = 0; i < m; i++) { t[i] = nl[net][i]; comp[i] = i; }
    t[m++] = NP + net; comp[m - 1] = m - 1;
    int na = 0;
    for (int i = 0; i < m - 1; i++) pidx[t[i]] = i;
    // adjacency merges (union by relabelling; lists are tiny)
    for (int i = 0; i < m - 1; i++) {
        int p = t[i];
        for (int k = 0; k < nadj[p]; k++) {
            int q = adjn[p][k];
            if (pnet[q] != net) continue;
            int j = pidx[q];
            if (comp[i] == comp[j]) continue;
            int cj = comp[j], ci = comp[i];
            for (int z = 0; z < m; z++) if (comp[z] == cj) comp[z] = ci;
            na++;
        }
    }
    // compact component ids
    int cid[40], nco = 0, id_of[40];
    for (int i = 0; i < 40; i++) id_of[i] = -1;
    for (int i = 0; i < m; i++) { if (id_of[comp[i]] < 0) id_of[comp[i]] = nco++; cid[i] = id_of[comp[i]]; }
    double D[40][40];
    for (int i = 0; i < nco; i++) for (int j = 0; j < nco; j++) D[i][j] = 1e18;
    for (int i = 0; i < m; i++) {
        const double *Wi = W[t[i]];
        for (int j = i + 1; j < m; j++) {
            int ci = cid[i], cj = cid[j];
            if (ci == cj) continue;
            double w = Wi[t[j]];
            if (w < D[ci][cj]) { D[ci][cj] = w; D[cj][ci] = w; }
        }
    }
    double key[40]; int in[40];
    for (int i = 0; i < nco; i++) { key[i] = 1e18; in[i] = 0; }
    key[0] = 0; double tot = na * ADJ_W; int nlg = 0;
    for (int it = 0; it < nco; it++) {
        int b2 = -1; double kb = 1e30;
        for (int i = 0; i < nco; i++) if (!in[i] && key[i] < kb) { kb = key[i]; b2 = i; }
        in[b2] = 1; if (it > 0) { tot += kb; nlg++; }
        for (int i = 0; i < nco; i++) if (!in[i] && D[b2][i] < key[i]) key[i] = D[b2][i];
    }
    if (n_adj_out) *n_adj_out = na;
    if (n_long_out) *n_long_out = nlg;
    return tot;
}

static void rebuild_lists(void) {
    for (int k = 0; k < L; k++) nc[k] = 0;
    for (int p = 0; p < NP; p++) { int k = padnet(p); pnet[p] = k; nl[k][nc[k]++] = p; }
}

static void move_pad(int p, int from, int to) {
    pnet[p] = to;
    for (int i = 0; i < nc[from]; i++) if (nl[from][i] == p) { nl[from][i] = nl[from][--nc[from]]; break; }
    nl[to][nc[to]++] = p;
}

/* involutions: calling twice restores the state */
static void apply_swap(int i, int j) {
    int old[4] = { padnet(2 * i), padnet(2 * i + 1), padnet(2 * j), padnet(2 * j + 1) };
    int ta = pa[i], tc = pc[i]; pa[i] = pa[j]; pc[i] = pc[j]; pa[j] = ta; pc[j] = tc;
    int pads[4] = { 2 * i, 2 * i + 1, 2 * j, 2 * j + 1 };
    for (int k = 0; k < 4; k++) { int nw = padnet(pads[k]); if (nw != old[k]) move_pad(pads[k], old[k], nw); }
}
static void apply_flip(int i) {
    int o0 = padnet(2 * i), o1 = padnet(2 * i + 1);
    pf[i] ^= 1;
    move_pad(2 * i, o0, padnet(2 * i)); move_pad(2 * i + 1, o1, padnet(2 * i + 1));
}
static void apply_label(int x, int y) {
    for (int p = 0; p < N; p++) {
        if (pa[p] == x) pa[p] = y; else if (pa[p] == y) pa[p] = x;
        if (pc[p] == x) pc[p] = y; else if (pc[p] == y) pc[p] = x;
    }
    int tmp[40]; int tc = nc[x];
    memcpy(tmp, nl[x], sizeof tmp); memcpy(nl[x], nl[y], sizeof tmp); memcpy(nl[y], tmp, sizeof tmp);
    nc[x] = nc[y]; nc[y] = tc;
    for (int i = 0; i < nc[x]; i++) pnet[nl[x][i]] = x;
    for (int i = 0; i < nc[y]; i++) pnet[nl[y][i]] = y;
}

static double total_cost(void) {
    double s = 0;
    for (int k = 0; k < L; k++) { netcost[k] = mst_cost(k, NULL, NULL); s += netcost[k]; }
    return s;
}

static void build_tables(double *lx, double *ly, double *rx, double *ry) {
    for (int n = 0; n < N; n++) {
        px[2 * n] = lx[n] + 0.557; py[2 * n] = ly[n] - 0.557;      // A pad: up-right (y down)
        px[2 * n + 1] = lx[n] - 0.557; py[2 * n + 1] = ly[n] + 0.557;
    }
    for (int i = 0; i < L; i++) { px[NP + i] = rx[i]; py[NP + i] = ry[i]; }
    for (int i = 0; i < NT; i++) for (int j = 0; j < NT; j++) {
        double d = hypot(px[i] - px[j], py[i] - py[j]);
        if (i < NP && j < NP && d < 1.55 && (i >> 1) != (j >> 1)) W[i][j] = ADJ_W;
        else W[i][j] = pow(d, LEN_EXP) + VIA_PEN;
    }
}

static void report(const char *tag) {
    double tot = 0; int fr = 0, adj = 0, lng = 0;
    for (int k = 0; k < L; k++) {
        int na, nl; double c = mst_cost(k, &na, &nl);
        tot += c; adj += na; lng += nl; fr += nl + 1;
    }
    printf("%s cost=%.1f adj_edges=%d long_edges=%d fragments=%d\n", tag, tot, adj, lng, fr);
}

int main(int argc, char **argv) {
    const char *posfile = argv[1];
    double iters = atof(argv[2]);
    uint64_t seed = (uint64_t)atoll(argv[3]);
    const char *outfile = argv[4];
    double T0 = 3.0, T1 = 0.03;
    const char *initfile = NULL;
    for (int i = 5; i < argc; i++) {
        if (!strcmp(argv[i], "-flip")) ALLOW_FLIP = 1;
        else if (!strcmp(argv[i], "-adj")) ADJ_W = atof(argv[++i]);
        else if (!strcmp(argv[i], "-via")) VIA_PEN = atof(argv[++i]);
        else if (!strcmp(argv[i], "-T0")) T0 = atof(argv[++i]);
        else if (!strcmp(argv[i], "-T1")) T1 = atof(argv[++i]);
        else if (!strcmp(argv[i], "-init")) initfile = argv[++i];
    }
    rs = seed * 0x9E3779B97F4A7C15ULL + 88172645463325252ULL; for (int i = 0; i < 10; i++) rnd();
    FILE *f = fopen(posfile, "r"); int n = 0; if (f && fscanf(f, "%d", &n) != 1) n = 0;
    double lx[N], ly[N];
    if (!f || n != N) { fprintf(stderr, "bad positions file\n"); return 1; }
    for (int i = 0; i < N; i++) if (fscanf(f, "%lf %lf", &lx[i], &ly[i]) != 2) { fprintf(stderr, "bad positions file\n"); return 1; }
    fclose(f);
    double rx[L], ry[L];
    for (int i = 0; i < L; i++) { rx[i] = -9.0 + 1.2 * i; ry[i] = 55.79; }
    build_tables(lx, ly, rx, ry);
    for (int p = 0; p < NP; p++) { nadj[p] = 0; for (int q = 0; q < NP; q++) if (q != p && (p >> 1) != (q >> 1) && W[p][q] == ADJ_W) adjn[p][nadj[p]++] = q; }
    for (int i = 0; i < N; i++) { near_cnt[i] = 0; for (int j = 0; j < N; j++) if (j != i && hypot(lx[i] - lx[j], ly[i] - ly[j]) < 9.0) near_list[i][near_cnt[i]++] = j; }
    // start: a given arrangement or a random permutation of the 240 ordered pairs
    int pairs[N][2], np = 0;
    for (int a = 0; a < L; a++) for (int c = 0; c < L; c++) if (a != c) { pairs[np][0] = a; pairs[np][1] = c; np++; }
    if (initfile) {
        FILE *fi = fopen(initfile, "r");
        if (!fi) { fprintf(stderr, "cannot open %s\n", initfile); return 1; }
        for (int i = 0; i < N; i++) if (fscanf(fi, "%d %d %d", &pa[i], &pc[i], &pf[i]) != 3) { fprintf(stderr, "bad init file\n"); return 1; }
        fclose(fi);
    }
    else {
        for (int i = N - 1; i > 0; i--) { int j = irand(i + 1); int t0 = pairs[i][0], t1 = pairs[i][1]; pairs[i][0] = pairs[j][0]; pairs[i][1] = pairs[j][1]; pairs[j][0] = t0; pairs[j][1] = t1; }
        for (int i = 0; i < N; i++) { pa[i] = pairs[i][0]; pc[i] = pairs[i][1]; pf[i] = 0; }
    }
    rebuild_lists();
    double cost = total_cost();
    report("start");
    long long it_total = (long long)iters;
    double T = T0, alpha = pow(T1 / T0, 1.0 / (double)it_total);
    double best = cost; int ba[N], bc[N], bf[N];
    memcpy(ba, pa, sizeof pa); memcpy(bc, pc, sizeof pc); memcpy(bf, pf, sizeof pf);
    long long acc = 0;
    for (long long it = 0; it < it_total; it++, T *= alpha) {
        int mv = irand(100);
        int nets[4], nn = 0;
        int i = -1, j = -1, lab_i = -1, lab_j = -1;
        if (mv < 3) { // label swap
            lab_i = irand(L); lab_j = irand(L); if (lab_i == lab_j) continue;
            nets[nn++] = lab_i; nets[nn++] = lab_j;
            apply_label(lab_i, lab_j);
        } else if (ALLOW_FLIP && mv < 13) { // flip orientation
            i = irand(N); nets[nn++] = pa[i]; nets[nn++] = pc[i]; apply_flip(i);
        } else { // swap pairs of two positions (mostly near each other)
            i = irand(N);
            if (urand() < 0.6) j = near_list[i][irand(near_cnt[i])];
            else j = irand(N);
            if (i == j) continue;
            nets[nn++] = pa[i]; nets[nn++] = pc[i]; nets[nn++] = pa[j]; nets[nn++] = pc[j];
            apply_swap(i, j);
        }
        int un[4], nu = 0;
        for (int k = 0; k < nn; k++) { int d = 0; for (int q = 0; q < nu; q++) if (un[q] == nets[k]) d = 1; if (!d) un[nu++] = nets[k]; }
        double old = 0, nw = 0, newc[4];
        for (int k = 0; k < nu; k++) { old += netcost[un[k]]; newc[k] = mst_cost(un[k], NULL, NULL); nw += newc[k]; }
        double d = nw - old;
        if (d <= 0 || urand() < exp(-d / T)) {
            for (int k = 0; k < nu; k++) netcost[un[k]] = newc[k];
            cost += d; acc++;
            if (cost < best - 1e-9) { best = cost; memcpy(ba, pa, sizeof pa); memcpy(bc, pc, sizeof pc); memcpy(bf, pf, sizeof pf); }
        } else { // revert
            if (lab_i >= 0) apply_label(lab_i, lab_j);
            else if (j < 0) apply_flip(i);
            else apply_swap(i, j);
        }
        if ((it & 0xFFFFF) == 0) { fprintf(stderr, "seed %llu it %lld T %.3f cost %.1f best %.1f acc %.3f\n", (unsigned long long)seed, it, T, cost, best, (double)acc / (it + 1)); }
    }
    memcpy(pa, ba, sizeof pa); memcpy(pc, bc, sizeof pc); memcpy(pf, bf, sizeof pf);
    rebuild_lists();
    total_cost();
    report("final"); fflush(stdout);
    FILE *o = fopen(outfile, "w");
    for (int p = 0; p < N; p++) fprintf(o, "%d %d %d\n", pa[p], pc[p], pf[p]);
    fclose(o);
    return 0;
}
