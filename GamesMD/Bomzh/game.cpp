#include "game.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>

State S;
UIState ui;
Ask ask;
std::string msg, popup;
static std::vector<std::string> notes;

// ---------- random ----------
static int rmode = 0;  // 0 = normal, 1 = minimum rolls, 2 = maximum rolls (preview)
static double rnd() {
  if (rmode == 1) return 0.0;
  if (rmode == 2) return 0.99999;
  return platformRandom() / 4294967296.0;
}
static int64_t r(int64_t a, int64_t b) { return a + (int64_t)floor(rnd() * (double)(b - a + 1)); }
static bool ch(double p) { return !S.pv && rnd() < p; }  // incidents never fire in the preview

// ---------- text helpers ----------
std::string fmt(int64_t n) {
  bool neg = n < 0;
  uint64_t v = neg ? -n : n;
  std::string d = std::to_string(v), o;
  int c = 0;
  for (int i = (int)d.size() - 1; i >= 0; i--) {
    o.insert(o.begin(), d[i]);
    if (++c % 3 == 0 && i > 0) o.insert(o.begin(), ' ');
  }
  return neg ? "-" + o : o;
}
static std::string fmtd(double x) { return fmt((int64_t)llround(x)); }
static std::string f1(double x) { char b[16]; snprintf(b, sizeof b, "%.1f", x); return b; }
static std::string fc(const Cost& c) { return c.uah >= 0 ? fmt(c.uah) + "₴" : "$" + fmt(c.usd); }
static Cost U(int64_t v) { Cost c; c.uah = v; return c; }
static Cost D(int64_t v) { Cost c; c.usd = v; return c; }
static std::string sgn(int64_t v) { return v > 0 ? "+" + fmt(v) : v < 0 ? "−" + fmt(-v) : "0"; }

// "Потрібно: a, b" builder
struct Need {
  std::string s;
  Need& operator()(bool ok, const char* what) {
    if (!ok) { if (!s.empty()) s += ", "; s += what; }
    return *this;
  }
};

// ---------- calendar ----------
static int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int64_t)doe - 719468;
}
static void civil(int64_t z, int& y, int& m, int& d) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = (unsigned)(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  y = (int)(yoe + era * 400);
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  y += m <= 2;
}
static const int64_t T0 = daysFromCivil(2026, 9, 1);
static int64_t zOf(int day) { return T0 + day - 1; }
int weekday(int day) { int64_t z = zOf(day); return (int)(((z % 7) + 7 + 4) % 7); }
static int mday(int day) { int y, m, d; civil(zOf(day), y, m, d); return d; }
static int month(int day) { int y, m, d; civil(zOf(day), y, m, d); return m; }
std::string dd(int day) { int y, m, d; civil(zOf(day), y, m, d); char b[8]; snprintf(b, sizeof b, "%02d.%02d", d, m); return b; }
static int daysToRent() {
  int y, m, d; civil(zOf(S.day), y, m, d);
  int ny = m == 12 ? y + 1 : y, nm = m == 12 ? 1 : m + 1;
  return (int)(daysFromCivil(ny, nm, 1) - zOf(S.day));
}
static int dim() {
  int y, m, d; civil(zOf(S.day), y, m, d);
  int ny = m == 12 ? y + 1 : y, nm = m == 12 ? 1 : m + 1;
  return (int)(daysFromCivil(ny, nm, 1) - daysFromCivil(y, m, 1));
}
static int season() { int m = month(S.day); return (m == 12 || m <= 2) ? 0 : m <= 5 ? 1 : m <= 8 ? 2 : 3; }
static bool weekend() { int w = weekday(S.day); return w == 0 || w == 6; }

const char* WD[7] = {"Нд", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб"};
static const char* MON[12] = {"січня", "лютого", "березня", "квітня", "травня", "червня", "липня", "серпня", "вересня", "жовтня", "листопада", "грудня"};
static const char* SEASONS[4] = {"зима", "весна", "літо", "осінь"};
const char* WXN[8] = {"сніг", "мороз", "хмарно", "дощ", "ясно", "спека", "гроза", "туман"};
struct WxE { int t, base; double p; };
static const WxE WX[4][3] = {
  {{0, -4, .4}, {1, -14, .3}, {2, -1, .3}},
  {{3, 8, .35}, {4, 15, .45}, {2, 11, .2}},
  {{5, 32, .3}, {4, 25, .5}, {6, 21, .2}},
  {{3, 8, .4}, {7, 6, .2}, {4, 13, .4}},
};
static void genWx() {
  const WxE* L = WX[season()];
  double x = rnd();
  const WxE* w = &L[2];
  for (int i = 0; i < 3; i++) { if (x < L[i].p) { w = &L[i]; break; } x -= L[i].p; }
  S.wxT = w->t;
  S.wxC = w->base + (int)r(-3, 3);
}
static bool wet() { return S.wxT == 0 || S.wxT == 3 || S.wxT == 6; }

// ---------- data ----------
const char* TABS[NTABS] = {"СТАН", "ЗАРОБІТОК", "ЇЖА", "ЗДОРОВʼЯ", "НАСТРІЙ", "РЕЧІ", "ОСВІТА", "ЖИТЛО", "ТРАНСПОРТ", "РОБОТА", "БІЗНЕС", "ПОЛІТИКА", "ЗАХИСТ"};
static const char* CLOTH[4] = {"Лахміття", "Секонд", "Нормальний", "Діловий костюм"};
static const char* EDU[4] = {"Немає", "Коледж", "Університет", "MBA"};

struct House { const char* n; Cost cost, rent; bool mon; };
static House HOUSES[8];
static const int PROT[8] = {0, 1, 1, 2, 2, 3, 3, 4};
static const int COLDHP[8] = {6, 4, 2, 1, 0, 0, 0, 0};

struct Trans { const char* n; Cost cost; const char* desc; };
static Trans TRANS[5];
static const double TRB[5] = {1, 1.1, 1.3, 1.5, 1.6};
static const int TRM[5] = {0, 0, 0, 1, 2};
static const double TRF[5] = {0, 0, 0, .1, .2};

struct Bag { const char* n; int cap; int64_t price; };
static const Bag BAGS[4] = {{"Кишені", 15, 0}, {"Пакет-майка", 40, 5}, {"Сумка-кравчучка", 100, 250}, {"Візок із супермаркету", 300, 900}};

struct Job { const char* n; int64_t uah, usd; };
static const Job JOBS[8] = {{"Вантажник на ринку", 350, -1}, {"Курʼєр", 600, -1}, {"Доставка піци", 700, -1}, {"Охоронець", 800, -1},
                            {"Таксист", 1500, -1}, {"Офіс-менеджер", -1, 40}, {"Програміст", -1, 120}, {"Директор", -1, 400}};

struct Biz { const char* n; Cost cost; int lo, hi; bool usd; };
static Biz BIZ[6];

static const char* POL[6] = {"Ніхто", "Громадський активіст", "Депутат міськради", "Мер міста", "Народний депутат", "Президент"};
static const int POL_PAY[6] = {0, 0, 30, 150, 300, 1000};
struct Step { const char* n; Cost cost; int rep, days; bool sure; };
static Step STEPS[5];

static void initTables() {
  static bool done = false;
  if (done) return;
  done = true;
  HOUSES[0] = {"Лавка в парку", Cost(), Cost(), false};
  HOUSES[1] = {"Картонна коробка", U(50), Cost(), false};
  HOUSES[2] = {"Підвал", U(300), Cost(), false};
  HOUSES[3] = {"Ночліжка", Cost(), U(80), false};
  HOUSES[4] = {"Кімната в гуртожитку", Cost(), U(4000), true};
  HOUSES[5] = {"Оренда квартири", Cost(), D(350), true};
  HOUSES[6] = {"Своя квартира", D(40000), Cost(), false};
  HOUSES[7] = {"Котедж", D(150000), Cost(), false};
  TRANS[0] = {"Пішки", Cost(), "Ходиш пішки. Без бонусів."};
  TRANS[1] = {"Велосипед б/в", U(2500), "Курʼєр, елітний район. Пляшок +10%."};
  TRANS[2] = {"Мопед", U(15000), "Курʼєр +50%, піца, траса. Пляшок +30%, робота втомлює вдвічі менше."};
  TRANS[3] = {"Старе авто", D(2000), "Таксі. Пляшок +50%, настрій +1/день, легше знайти друзів. Бонуси мопеда."};
  TRANS[4] = {"Нове авто", D(25000), "Директор. Пляшок +60%, настрій +2/день, ще легше знайти друзів. Бонуси мопеда."};
  BIZ[0] = {"Пункт прийому склотари", U(20000), 150, 450, false};
  BIZ[1] = {"Кіоск із шаурмою", U(60000), 500, 1400, false};
  BIZ[2] = {"Автомийка", D(5000), 15, 50, true};
  BIZ[3] = {"Магазин біля дому", D(20000), 60, 180, true};
  BIZ[4] = {"Мережа кавʼярень", D(100000), 300, 800, true};
  BIZ[5] = {"IT-компанія", D(500000), 1500, 3500, true};
  STEPS[0] = {"Вступити в громадську організацію", U(1000), 5, 1, true};
  STEPS[1] = {"Вибори в міськраду", D(2000), 40, 30, false};
  STEPS[2] = {"Вибори мера", D(20000), 120, 30, false};
  STEPS[3] = {"Вибори до парламенту", D(100000), 250, 30, false};
  STEPS[4] = {"Президентські вибори", D(500000), 500, 60, false};
}

static std::string bizNeed(int i) {
  Need n;
  switch (i) {
    case 0: case 1: case 2: n(S.docs, "документи"); break;
    case 3: n(S.edu >= 1, "коледж"); break;
    case 4: n(S.edu >= 2, "університет"); break;
    case 5: n(S.edu >= 3, "MBA")(S.cloth >= 3, "костюм"); break;
  }
  return n.s;
}
static std::string stepNeed(int i) {
  Need n;
  switch (i) {
    case 0: n(S.docs, "документи"); break;
    case 1: n(S.edu >= 1, "коледж")(S.cloth >= 2, "нормальний одяг"); break;
    case 2: n(S.edu >= 2, "університет"); break;
    case 3: n(S.cloth >= 3, "костюм"); break;
    case 4: n(S.edu >= 3, "MBA"); break;
  }
  return n.s;
}
static std::string jobNeed(int i) {
  Need n;
  switch (i) {
    case 0: n(S.cloth >= 1, "одяг із секонду"); break;
    case 1: n(S.trans >= 1, "велосипед")(S.phone, "телефон"); break;
    case 2: n(S.trans >= 2, "мопед")(S.phone, "телефон"); break;
    case 3: n(S.docs, "документи")(S.cloth >= 2, "нормальний одяг")(S.rep >= 10, "репутація 10"); break;
    case 4: n(S.trans >= 3, "авто")(S.lic, "права")(S.phone, "телефон"); break;
    case 5: n(S.edu >= 1, "коледж")(S.cloth >= 2, "нормальний одяг")(S.docs, "документи")(S.rep >= 25, "репутація 25"); break;
    case 6: n(S.edu >= 2, "університет")(S.phone, "телефон"); break;
    case 7: n(S.edu >= 3, "MBA")(S.cloth >= 3, "костюм")(S.trans >= 4, "нове авто")(S.rep >= 100, "репутація 100"); break;
  }
  return n.s;
}
static std::string houseNeed(int i) {
  Need n;
  if (i == 4) n(S.docs, "документи");
  if (i == 5) n(S.docs, "документи")(S.cloth >= 2, "нормальний одяг");
  return n.s;
}
static std::string transNeed(int i) { Need n; if (i >= 2) n(S.lic, "права"); return n.s; }

// ---------- derived values ----------
static int lvlB() { return std::min(10, S.skB / 12); }
static int64_t bagCap() { return BAGS[S.bag].cap; }
int64_t cap() { return S.biz[0] ? S.wh : bagCap(); }
static double wm() { return (S.press ? 2 : 1.5) - (S.comp ? .4 : 0); }
static int64_t putBot(int64_t n) { int64_t g = std::max<int64_t>(0, std::min<int64_t>(n, cap() - S.bot)); S.bot += g; return g; }
static double jobMul(int i) { return 1 + std::min(1.0, S.jx[i] / 60.0); }
static double jobTm(int i) { return i == 1 && S.trans >= 2 ? 1.5 : 1; }
static int bizN() { int n = 0; for (int i = 0; i < 6; i++) n += S.biz[i] ? 1 : 0; return n; }
static bool copOk() { return S.day < S.copUntil; }
static double repMul() { return 1 + std::min(1.0, S.rep / 100.0); }
static double copF() { return std::max(.3, 1 - S.rep / 100.0); }
static void loseRep(int n) { S.rep -= n + (S.pol >= 2 && S.rh != S.day ? 1 : 0); S.rh = S.day; }
static int64_t guardCost() {
  double s = 0;
  for (int i = 0; i < 6; i++) if (S.biz[i]) s += (BIZ[i].lo + BIZ[i].hi) / 2.0 * (BIZ[i].usd ? S.rate : 1);
  return std::max<int64_t>(1000, llround(s * .1 * 30));
}
static Cost pro(const House& h) {  // first month is charged only for the days left until the 1st
  if (!h.rent.any()) return h.cost;
  if (!h.mon) return h.rent;
  double f = (double)daysToRent() / dim();
  return h.rent.uah >= 0 ? U((int64_t)ceil(h.rent.uah * f)) : D((int64_t)ceil(h.rent.usd * f));
}

static void note(const std::string& t) { if (!S.pv) notes.push_back(t); }

bool canPay(const Cost& c) { return !c.any() || (c.uah >= 0 ? S.uah >= c.uah : S.usd >= c.usd); }
static void pay(const Cost& c) { if (c.uah >= 0) S.uah -= c.uah; else if (c.usd >= 0) S.usd -= c.usd; }
static bool payRent(const Cost& c) {  // hryvnia from cash first, then the card; dollars from cash
  if (c.usd >= 0) { if (S.usd < c.usd) return false; S.usd -= c.usd; return true; }
  if (S.uah >= c.uah) { S.uah -= c.uah; return true; }
  if (S.card && S.uah + S.bank >= c.uah) { S.bank -= c.uah - S.uah; S.uah = 0; return true; }
  return false;
}
static void clamp() {
  S.hp = std::max(0, std::min(100, S.hp));
  S.food = std::max(0, std::min(100, S.food));
  S.mood = std::max(0, std::min(100, S.mood));
  S.rep = std::max(0, S.rep);
}

// ---------- protection ----------
static std::string saved() {
  if (S.spray > 0) {
    S.spray--;
    return std::string("Злодій поліз у кишеню, але отримав газовим балончиком!") + (S.spray ? " Балончиків: " + std::to_string(S.spray) + "." : " Балончик закінчився.");
  }
  if (S.dog && rnd() < .5) return "Злодій підкрався, але собака загавкав, і той утік!";
  return "";
}
static std::string fight(double p, int dmg, const char* m) {
  if (!ch(S.selfdef ? p / 2 : p)) return "";
  if (S.fr >= 3 && rnd() < .5) return "Почалася бійка, але друзі заступилися.";
  S.hp -= dmg;
  loseRep(2);
  return std::string(m) + " Репутація −2.";
}

// ---------- bottles ----------
static std::string bottles(int64_t base, std::string extra) {
  int sz = season();
  double sm = sz == 2 ? 1.5 : sz == 0 ? .6 : 1;
  int64_t b = llround(base * sm * (1 + lvlB() * .1) * TRB[S.trans]);
  S.skB++;
  if (S.lost) { b = 0; S.lost = 0; }
  int64_t room = std::min<int64_t>(S.biz[0] ? bagCap() : (int64_t)1e12, cap() - S.bot);
  int64_t got = std::min<int64_t>(b, std::max<int64_t>(0, room));
  S.bot += got;
  std::string m = "+" + fmt(got) + " пляшок";
  if (got < b) m += " (не влізло " + fmt(b - got) + ", " + (S.biz[0] && cap() - S.bot > 0 ? "за раз більше не винесеш — потрібна більша сумка" : "потрібна більша сумка або склад") + ")";
  m += ".";
  if (!extra.empty()) m += " " + extra;
  return m;
}
static const char* SPOTN[7] = {"Парк", "Вокзал", "Стадіон", "Пляж", "Траса й заправки", "Біля нічного клубу", "Елітний район"};
static std::string spotNeed(int i) {
  Need n;
  if (i == 3) n(season() == 2, "літо");
  if (i == 4) n(S.trans >= 2, "мопед");
  if (i == 6) n(S.trans >= 1, "велосипед");
  return n.s;
}
// risk is rolled before the haul is counted, exactly like the prototype
static std::string runSpot(int i) {
  std::string m;
  switch (i) {
    case 0: {
      int64_t base = r(2, 7);
      std::string ex;
      if (ch(.1)) { if (S.dog && rnd() < .5) ex = "Конкуренти хотіли відібрати пляшки, але собака їх відігнав."; else S.half = 1; }
      m = bottles(base, ex);
      break;
    }
    case 1: {
      int64_t base = r(4, 12);
      std::string ex;
      if (ch(copOk() ? 0 : (S.docs && S.cloth >= 1 ? .07 : .15) * copF())) { S.lost = 1; S.mood -= 8; loseRep(1); ex = "Поліція відібрала пляшки! Репутація −1."; }
      m = bottles(base, ex);
      break;
    }
    case 2: {
      int64_t base = weekend() ? r(12, 28) : r(1, 5);
      std::string ex = weekend() ? fight(.1, 10, "Фанати штовхнули, здоровʼя −10.") : "";
      m = bottles(base, ex);
      break;
    }
    case 3: { int64_t base = r(12, 30); std::string ex; if (ch(.15)) { S.hp -= 5; ex = "Обгорів на сонці."; } m = bottles(base, ex); break; }
    case 4: { int64_t base = r(10, 24); std::string ex; if (ch(.08)) { S.hp -= 8; ex = "Ледь не зачепила фура, здоровʼя −8."; } m = bottles(base, ex); break; }
    case 5: { S.mood -= 3; int64_t base = r(6, 18); std::string ex = fight(.2, 15, "Нарвався на бійку, здоровʼя −15."); m = bottles(base, ex); break; }
    case 6: {
      int64_t base = r(8, 20);
      std::string ex;
      if (ch(.1)) { S.lost = 1; S.mood -= 10; ex = "Охорона прогнала."; }
      else if (ch(.12)) { int64_t u = r(150, 600); S.uah += u; ex = "Серед пляшок знайшов годинник: +" + fmt(u) + "₴."; }
      m = bottles(base, ex);
      break;
    }
  }
  if (S.half) { S.half = 0; int64_t k = S.bot / 2; S.bot -= k; m += " Конкуренти відібрали " + fmt(k) + "."; }
  return m;
}

// ---------- actions ----------
enum {
  A_SPOT = 1, A_SELLBOT, A_COINS, A_BEG, A_WASH, A_BUSK, A_BUY, A_BUYALL, A_SELL, A_SELLALL,
  A_FOOD, A_HEAL, A_MOOD, A_BAG, A_ITEM, A_EDU, A_HOUSE, A_TRANS, A_JOB, A_BIZ,
  A_WH, A_HIRE, A_FIRE, A_BUYBOT, A_PRESS, A_CONTRACT, A_LURE, A_ROOF,
  A_POL, A_STEP, A_SPRAY, A_DOG, A_CARD, A_DEPOSIT, A_W500, A_WALL, A_COP, A_SELFDEF, A_GUARD, A_LAWYER
};

static int64_t buyC(int64_t n) { return llround(S.rate * n); }
static int64_t sellC(int64_t n) { return llround(S.rate * .96 * n); }
static int64_t maxBuy() { return (int64_t)floor(S.uah / (S.rate * 10)) * 10; }
static int64_t maxSell() { return S.usd / 10 * 10; }
static Cost jobPay(int i) {
  double m = jobMul(i) * jobTm(i);
  return JOBS[i].uah >= 0 ? U(llround(JOBS[i].uah * m)) : D(llround(JOBS[i].usd * m));
}

static std::string run(int id, int a) {
  switch (id) {
    case A_SPOT: return runSpot(a);
    case A_SELLBOT: { int64_t u = (int64_t)floor(S.bot * S.brate), b = S.bot; S.uah += u; S.bot = 0; return "Здав " + fmt(b) + " пляшок за " + fmt(u) + "₴."; }
    case A_COINS: { int64_t u = r(0, 20); S.uah += u; return u ? "Знайшов " + fmt(u) + "₴ на тротуарі." : "Нічого не знайшов."; }
    case A_BEG: {
      int64_t u = llround(r(5, 45) * repMul()); S.uah += u; S.mood -= 6;
      bool rl = S.rep > 0; loseRep(1);
      return "Подали " + fmt(u) + "₴. Соромно, але їсти хочеться." + (rl ? " Репутація −1." : "");
    }
    case A_WASH: { int64_t u = r(80, 160); S.uah += u; S.food -= 5; return "Помив машини: +" + fmt(u) + "₴."; }
    case A_BUSK: { int64_t u = llround(r(30, 250) * repMul()); S.uah += u; S.mood += 6; return "Перехожі накидали " + fmt(u) + "₴."; }
    case A_BUY: { S.usd += a; return "Купив $" + fmt(a) + " за " + fmt(buyC(a)) + "₴."; }
    case A_BUYALL: { int64_t n = maxBuy(), c = buyC(n); S.uah -= c; S.usd += n; return "Купив $" + fmt(n) + " за " + fmt(c) + "₴."; }
    case A_SELL: { S.usd -= a; S.uah += sellC(a); return "Продав $" + fmt(a) + " за " + fmt(sellC(a)) + "₴."; }
    case A_SELLALL: { int64_t n = maxSell(), u = sellC(n); S.usd -= n; S.uah += u; return "Продав $" + fmt(n) + " за " + fmt(u) + "₴."; }
    case A_FOOD:
      switch (a) {
        case 0: {
          S.food += 20; S.mood -= 5; bool rl = S.rep > 0; loseRep(1);
          int64_t h = r(0, 12); S.hp -= (int)h;
          return std::string(h > 6 ? "Поїв на смітнику. Крутить живіт." : "Поїв на смітнику. Цього разу пощастило.") + (rl ? " Люди бачили: репутація −1." : "");
        }
        case 1: S.food += 15; return "Зʼїв пиріжок з картоплею.";
        case 2: S.food += 35; S.mood += 3; return "Борщ і котлета. Жити можна.";
        case 3: S.food += 45; S.mood += 8; return "Велика шаурма. Ситно!";
        case 4: S.food += 70; S.hp += 5; return "Приготував нормальну вечерю.";
        case 5: S.food += 100; S.mood += 30; return "Вечеря в ресторані. Відчуваєш себе людиною.";
        case 6: S.food = 100; S.hp += 15; S.mood += 10; return "Кухар приготував тиждень правильної їжі.";
      }
      break;
    case A_HEAL:
      switch (a) {
        case 0: {
          int64_t h = r(-5, 25); S.hp += (int)h; bool rl = S.rep > 0; loseRep(1);
          return (h >= 0 ? "Допомогло: +" + fmt(h) + "." : "Стало гірше: " + fmt(h) + ".") + (rl ? " Репутація −1." : "");
        }
        case 1: S.hp += 10; return "Чай з малиною і цибуля. Трохи краще.";
        case 2: S.hp += 8; S.food -= 10; S.mood += 2; return "Пробіг 3 км. Здоровʼя +8.";
        case 3: S.hp += 25; return "Купив ліки в аптеці.";
        case 4: S.hp += 40; return "Лікар виписав лікування.";
        case 5: S.hp += 70; return "Повне обстеження і лікування.";
        case 6: S.hp = 100; S.mood += 20; return "Тиждень у клініці за кордоном. Як новий.";
      }
      break;
    case A_MOOD:
      switch (a) {
        case 0: S.mood += 6; S.food -= 2; return "Голуби вдячні. Настрій +6.";
        case 1:
          S.mood += 3; S.lastFr = S.day;
          if (S.fr < 10 && rnd() < .35 + S.cloth * .1 + TRF[S.trans] + std::min(.2, S.rep / 200.0)) { S.fr++; return "Знайшов нового друга! Друзів: " + std::to_string(S.fr) + "."; }
          return "Поговорили, але не здружилися.";
        case 2: S.mood += std::min(35, 12 + S.fr * 2); S.hp -= 3; S.lastFr = S.day; return "Посиділи компанією з " + std::to_string(S.fr) + " друзів.";
        case 3: S.mood += 4; S.lastFr = S.day; return "Поговорили по телефону. Друзі памʼятають про тебе.";
        case 4: S.mood += 25; return "Сходив у кіно на новий фільм.";
        case 5: S.mood += 35; return "Наші виграли! Настрій +35.";
        case 6: S.mood += 50; return "Концерт улюбленого гурту.";
        case 7: S.mood = 100; S.hp += 20; return "Тиждень у горах. Повний релакс.";
      }
      break;
    case A_BAG: S.bag = a; return "Тепер влазить " + std::to_string(BAGS[a].cap) + " пляшок.";
    case A_ITEM:
      switch (a) {
        case 0: S.cloth = 1; return "Тепер виглядаєш пристойно.";
        case 1: S.cloth = 2; return "Новий одяг. Тебе вже не цураються.";
        case 2: S.cloth = 3; return "Костюм сидить як влитий.";
        case 3: S.docs = 1; return "Три дні в черзі — і паспорт у руках!";
        case 4: S.phone = 1; return "Купив старенький смартфон.";
        case 5: S.guitar = 1; return "Гітара є. Можна грати в переході.";
      }
      break;
    case A_EDU:
      switch (a) {
        case 0: S.mood += 4; return "Тепло, тихо і книжки.";
        case 1: S.lic = 1; return "Здав на права з першого разу!";
        case 2: S.edu = 1; return "Отримав диплом коледжу.";
        case 3: S.edu = 2; return "Диплом університету!";
        case 4: S.edu = 3; return "Ти тепер магістр бізнесу.";
      }
      break;
    case A_HOUSE: {
      const House& h = HOUSES[a];
      bool down = a < S.house;
      S.house = a;
      if (down) return std::string("Переїхав: ") + h.n + ".";
      std::string m = std::string("Тепер: ") + h.n + ".";
      if (h.mon) m += " Сплатив " + fc(pro(h)) + " за " + std::to_string(daysToRent()) + " дн. до 1-го. Далі " + fc(h.rent) + " щомісяця.";
      return m;
    }
    case A_TRANS: S.trans = a; return std::string("Тепер: ") + TRANS[a].n + ".";
    case A_JOB: {
      double m = jobMul(a) * jobTm(a);
      Cost p = jobPay(a);
      if (p.uah >= 0) S.uah += p.uah; else S.usd += p.usd;
      int f0 = S.food;
      switch (a) {
        case 0: S.food -= 12; S.hp -= 4; S.mood -= 4; break;
        case 1: S.food -= 8; S.mood -= 3; break;
        case 2: S.food += 10; S.mood -= 2; break;
        case 3: S.mood -= 4; break;
        case 4: S.mood -= 4; break;
        case 5: S.mood -= 3; break;
        case 6: S.mood -= 2; break;
        case 7: S.mood -= 5; break;
      }
      if (S.trans >= 2 && S.food < f0) S.food = f0 - (int)llround((f0 - S.food) / 2.0);
      S.jx[a]++;
      double nm = jobMul(a) * jobTm(a);
      return "Відпрацював: +" + fc(p) + "." + (nm > m && floor(nm * 10) > floor(m * 10) ? " Досвід росте, зарплата теж!" : "");
    }
    case A_BIZ: {
      S.biz[a] = 1;
      const Biz& b = BIZ[a];
      std::string inc = b.usd ? "$" + fmt(b.lo) + "–" + fmt(b.hi) : fmt(b.lo) + "–" + fmt(b.hi) + "₴";
      return std::string("Купив «") + b.n + "». Дохід " + inc + " на день.";
    }
    case A_WH: S.wh += 1000; return "Склад тепер на " + fmt(S.wh) + " пляшок.";
    case A_HIRE: S.workers++; return "Найняв збирача. Їх тепер " + std::to_string(S.workers) + ", платиш " + fmt(100 * S.workers) + "₴ щодня.";
    case A_FIRE: S.workers--; return "Звільнив одного. Лишилось: " + std::to_string(S.workers) + ".";
    case A_BUYBOT: {
      int64_t n = std::min<int64_t>(std::min<int64_t>(llround(r(30, 80) * repMul()), S.uah), S.wh - S.bot);
      S.uah -= n; S.bot += n;
      return "Скупив " + fmt(n) + " пляшок за " + fmt(n) + "₴.";
    }
    case A_PRESS: S.press = 1; return "Поставив прес. Оптом тепер ×2.";
    case A_CONTRACT: S.contrUntil = std::max(S.day, S.contrUntil) + 30; return "Контракт діє до " + dd(S.contrUntil) + ".";
    case A_LURE: S.comp = 0; S.mood += 10; return "Люди обрали тебе. Конкурент закрився!";
    case A_ROOF: S.comp = 0; return "Конкурента «попросили» піти.";
    case A_POL:
      switch (a) {
        case 0: S.rep += 1; S.mood -= 3; return "Роздав листівки. Репутація +1.";
        case 1: S.rep += 3; S.mood += 5; return "Ти памʼятаєш, як це. Репутація +3.";
        case 2: S.rep += 6; S.mood -= 2; return "Тебе показали в новинах. Репутація +6.";
        case 3: S.rep += 25; return "Фонд допоміг сотням людей. Репутація +25.";
      }
      break;
    case A_STEP: {
      const Step& st = STEPS[S.pol];
      double p = st.sure ? 1 : std::min(.85, .35 + (double)(S.rep - st.rep) / st.rep * .5);
      if (S.pv || rnd() < p) {
        S.pol++;
        if (S.pol == 5) { if (!S.won) S.won = S.day; note("ПЕРЕМОГА! За " + fmt(S.day) + " днів ти пройшов шлях від лавки в парку до президента."); }
        return std::string("Тепер ти: ") + POL[S.pol] + "." + (POL_PAY[S.pol] ? " Зарплата $" + std::to_string(POL_PAY[S.pol]) + "/день." : "");
      }
      int l = (int)floor(S.rep * .3); S.rep -= l; S.mood -= 15;
      return "Програв вибори. Репутація −" + std::to_string(l) + ".";
    }
    case A_SPRAY: S.spray++; return "Балончик у кишені. Врятує від одного пограбування.";
    case A_DOG: S.dog = 1; S.dogH = 0; S.mood += 10; return "Тепер у тебе є вірний пес. Корм 20₴ щодня списується сам.";
    case A_CARD: S.card = 1; return "Картка є. Гроші на ній не вкрадуть.";
    case A_DEPOSIT: { int64_t u = S.uah; S.bank += u; S.uah = 0; return "Поклав " + fmt(u) + "₴ на картку. На картці: " + fmt(S.bank) + "₴."; }
    case A_W500: S.bank -= 505; S.uah += 500; return "Зняв 500₴. На картці: " + fmt(S.bank) + "₴.";
    case A_WALL: { int64_t f = (int64_t)ceil(S.bank * .01), u = S.bank - f; S.uah += u; S.bank = 0; return "Зняв " + fmt(u) + "₴, комісія " + fmt(f) + "₴."; }
    case A_COP: S.copUntil = std::max(S.day, S.copUntil) + 7; return "Поліція не чіпатиме тебе до " + dd(S.copUntil) + ".";
    case A_SELFDEF: S.selfdef = 1; S.hp += 5; return "Тепер у бійки потрапляєш удвічі рідше.";
    case A_GUARD: S.guardUntil = std::max(S.day, S.guardUntil) + 30; return "Охорона стереже бізнес до " + dd(S.guardUntil) + ".";
    case A_LAWYER: S.lawUntil = std::max(S.day, S.lawUntil) + 30; return "Юрист працює на тебе до " + dd(S.lawUntil) + ". Штрафи вдвічі менші.";
  }
  return "";
}

// ---------- action lists ----------
static Item A(const std::string& label, const std::string& price, int id, int arg = 0, int days = 1) {
  Item it; it.label = label; it.price = price; it.id = id; it.arg = arg; it.days = days; return it;
}
static Item INFO(const std::string& label, const std::string& price) { Item it; it.label = label; it.price = price; it.kind = K_INFO; return it; }
static std::string until(int u, const std::string& t) { return S.day < u ? t + " (до " + dd(u) + ")" : t; }

static void levels(std::vector<Item>& L, bool house) {
  int curL = house ? S.house : S.trans, n = house ? 8 : 5;
  for (int i = 0; i < n; i++) {
    const char* name = house ? HOUSES[i].n : TRANS[i].n;
    const char* desc = house ? "" : TRANS[i].desc;
    std::string rp = house && HOUSES[i].rent.any() ? fc(HOUSES[i].rent) + (HOUSES[i].mon ? "/міс" : "/д") : "";
    if (i == curL) {
      Item it = INFO(name, house && HOUSES[i].mon ? "✓ сплата " + dd(S.day + daysToRent()) : "✓ зараз");
      it.cur = true; it.desc = desc; L.push_back(it); continue;
    }
    if (i < curL) {
      if (house && (i == 0 || HOUSES[i].rent.any())) {
        Item it = A(name, i == 0 ? "переїхати" : rp, A_HOUSE, i, 0);
        if (HOUSES[i].rent.any()) it.cost = pro(HOUSES[i]);
        it.need = houseNeed(i);
        L.push_back(it);
      } else {
        Item it; it.label = name; it.price = "—"; it.kind = K_LOCKED; it.locked = "вже маєш краще"; it.desc = desc; L.push_back(it);
      }
      continue;
    }
    if (house) {
      Item it = A(name, !rp.empty() ? rp : fc(HOUSES[i].cost), A_HOUSE, i, 0);
      it.cost = pro(HOUSES[i]); it.need = houseNeed(i); L.push_back(it);
    } else {
      Item it = A(name, fc(TRANS[i].cost), A_TRANS, i, 0);
      it.cost = TRANS[i].cost; it.need = transNeed(i); it.desc = desc; L.push_back(it);
    }
  }
}

static void taraRows(std::vector<Item>& L) {
  L.push_back(INFO("— Пункт склотари —", "опт ×" + f1(wm())));
  L.push_back(INFO("Склад", fmt(S.bot) + " / " + fmt(S.wh) + " пл."));
  { Item it = A("Розширити склад (+1 000)", "8 000₴", A_WH, 0, 0); it.cost = U(8000); it.need = Need()(S.wh < 5000, "максимум 5 000").s; L.push_back(it); }
  { Item it = A("Найняти збирача (є: " + std::to_string(S.workers) + ")", "100₴/д", A_HIRE, 0, 0); it.need = Need()(S.workers < 10, "максимум 10").s;
    it.desc = "Кожен приносить 20–40 пляшок щодня. Інколи крадуть або йдуть."; L.push_back(it); }
  { Item it = A("Звільнити збирача", "", A_FIRE, 0, 0); it.need = Need()(S.workers > 0, "збирачі").s; L.push_back(it); }
  { Item it = A("Скупити пляшки в людей", "1₴/шт", A_BUYBOT); it.need = Need()(S.uah >= 1, "готівка")(S.bot < S.wh, "місце на складі").s;
    it.desc = "Купуєш по 1₴, а оптом здаєш по ~" + f1(S.brate * wm()) + "₴."; L.push_back(it); }
  { Item it = A("Прес для пластику", "25 000₴", A_PRESS, 0, 0); it.cost = U(25000); it.owned = S.press; it.desc = "Оптова ціна зростає з ×1,5 до ×2."; L.push_back(it); }
  { Item it = A(until(S.contrUntil, "Контракт: стадіон і клуби"), "6 000₴/міс", A_CONTRACT, 0, 0); it.cost = U(6000);
    it.desc = "Щосуботи й щонеділі привозять 80–200 пляшок."; L.push_back(it); }
  if (S.comp) {
    L.push_back(INFO("Конкурент поруч (до " + dd(S.comp) + ")", "опт −0,4"));
    { Item it = A("Переманити клієнтів", "безк.", A_LURE); it.need = Need()(S.rep >= 50, "репутація 50").s; L.push_back(it); }
    { Item it = A("Заплатити «даху»", "5 000₴", A_ROOF, 0, 0); it.cost = U(5000); L.push_back(it); }
  }
}

std::vector<Item> acts(int t) {
  initTables();
  std::vector<Item> L;
  auto add = [&](Item it, Cost c = Cost(), std::string need = "", bool owned = false) { it.cost = c; it.need = need; it.owned = owned; L.push_back(it); };
  switch (t) {
    case 0: {
      const House& h = HOUSES[S.house];
      L.push_back(INFO("Дата", std::string(WD[weekday(S.day)]) + ", " + std::to_string(mday(S.day)) + " " + MON[month(S.day) - 1]));
      L.push_back(INFO("Погода", std::string(WXN[S.wxT]) + ", " + std::to_string(S.wxC) + "° · " + SEASONS[season()]));
      L.push_back(INFO("Вік / прожито", std::to_string(S.age / 365) + " р. / " + fmt(S.day) + " дн."));
      L.push_back(INFO("Житло", h.n));
      if (h.rent.any()) L.push_back(INFO("Оплата житла", h.mon ? fc(h.rent) + " через " + std::to_string(daysToRent()) + " дн." : fc(h.rent) + " щодня"));
      L.push_back(INFO("Одяг / сумка", std::string(CLOTH[S.cloth]) + " / " + std::to_string(BAGS[S.bag].cap)));
      L.push_back(INFO("Освіта", std::string(EDU[S.edu]) + (S.lic ? " + права" : "")));
      L.push_back(INFO("Транспорт", TRANS[S.trans].n));
      L.push_back(INFO("Документи / телефон", std::string(S.docs ? "є" : "—") + " / " + (S.phone ? "є" : "—")));
      L.push_back(INFO("Друзі", S.fr ? std::to_string(S.fr) + " · бачились " + std::to_string(S.day - S.lastFr) + " дн. тому" : "0"));
      L.push_back(INFO("Навичка «пляшки»", "рів. " + std::to_string(lvlB()) + " (" + std::to_string(S.skB) + " походів)"));
      L.push_back(INFO("Посада / репутація", std::string(POL[S.pol]) + " / " + std::to_string(S.rep)));
      L.push_back(INFO("Бізнесів", std::to_string(bizN())));
      L.push_back(INFO("На картці", S.card ? fmt(S.bank) + "₴" : "немає картки"));
      {
        std::string p;
        auto ap = [&](bool c, const std::string& s) { if (c) { if (!p.empty()) p += ", "; p += s; } };
        ap(S.spray, "балончик×" + std::to_string(S.spray)); ap(S.dog, "собака"); ap(copOk(), "дільничний"); ap(S.selfdef, "самооборона");
        L.push_back(INFO("Захист", p.empty() ? "немає" : p));
      }
      L.push_back(INFO("Статки", fmtd(S.uah + S.bank + S.usd * S.rate) + "₴"));
      L.push_back(INFO("Рекорд", fmt(S.best) + " дн."));
      Item rs; rs.label = "Почати нову гру"; rs.kind = K_RESET; L.push_back(rs);
      break;
    }
    case 1: {
      std::string lv = "рів. " + std::to_string(lvlB());
      for (int i = 0; i < 7; i++) {
        std::string n = std::string("Пляшки: ") + SPOTN[i] + (i == 2 && weekend() ? " (матч!)" : "");
        add(A(n, lv, A_SPOT, i), Cost(), spotNeed(i));
      }
      add(A("Здати пляшки (" + fmt(S.bot) + ")", f1(S.brate) + "₴/шт", A_SELLBOT, 0, 0), Cost(), Need()(S.bot > 0, "пляшки").s);
      add(A("Шукати дрібʼязок", "безк.", A_COINS));
      add(A("Просити милостиню", "безк.", A_BEG));
      add(A("Мити машини", "безк.", A_WASH), Cost(), Need()(S.cloth >= 1, "одяг із секонду").s);
      add(A("Грати в переході", "безк.", A_BUSK), Cost(), Need()(S.guitar, "гітара").s);
      int64_t mb = maxBuy(), ms = maxSell();
      add(A("Купити $10", fmt(buyC(10)) + "₴", A_BUY, 10, 0), U(buyC(10)));
      add(A("Купити $100", fmt(buyC(100)) + "₴", A_BUY, 100, 0), U(buyC(100)));
      add(A("Купити на всі ($" + fmt(mb) + ")", fmt(buyC(mb)) + "₴", A_BUYALL, 0, 0), Cost(), Need()(mb >= 10, ("≥" + fmt(buyC(10)) + "₴").c_str()).s);
      add(A("Продати $10", fmt(sellC(10)) + "₴", A_SELL, 10, 0), Cost(), Need()(S.usd >= 10, "$10").s);
      add(A("Продати $100", fmt(sellC(100)) + "₴", A_SELL, 100, 0), Cost(), Need()(S.usd >= 100, "$100").s);
      add(A("Продати всі ($" + fmt(ms) + ")", fmt(sellC(ms)) + "₴", A_SELLALL, 0, 0), Cost(), Need()(ms >= 10, "$10").s);
      break;
    }
    case 2:
      add(A("Поїсти на смітнику", "безк.", A_FOOD, 0));
      add(A("Пиріжок у кіоску", "25₴", A_FOOD, 1), U(25));
      add(A("Обід у їдальні", "90₴", A_FOOD, 2), U(90));
      add(A("Шаурма", "150₴", A_FOOD, 3), U(150));
      add(A("Продукти в супермаркеті", "400₴", A_FOOD, 4), U(400), Need()(S.house >= 3, "житло з кухнею").s);
      add(A("Ресторан", "1 500₴", A_FOOD, 5), U(1500), Need()(S.cloth >= 2, "нормальний одяг").s);
      add(A("Особистий кухар", "$300", A_FOOD, 6), D(300), Need()(S.house >= 6, "своя квартира").s);
      break;
    case 3:
      add(A("Таблетки зі смітника", "безк.", A_HEAL, 0));
      add(A("Народні засоби", "60₴", A_HEAL, 1), U(60));
      add(A("Пробіжка в парку", "безк.", A_HEAL, 2));
      add(A("Аптека", "250₴", A_HEAL, 3), U(250));
      add(A("Поліклініка", "500₴", A_HEAL, 4), U(500), Need()(S.docs, "документи").s);
      add(A("Приватна клініка", "$50", A_HEAL, 5), D(50), Need()(S.docs, "документи").s);
      add(A("Лікування за кордоном", "$1 000", A_HEAL, 6, 7), D(1000), Need()(S.docs, "документи").s);
      break;
    case 4:
      add(A("Годувати голубів", "безк.", A_MOOD, 0));
      add(A("Познайомитися з кимось", "безк.", A_MOOD, 1));
      add(A("Пиво з друзями", "60₴", A_MOOD, 2), U(60), Need()(S.fr >= 1, "друзі").s);
      add(A("Подзвонити друзям", "безк.", A_MOOD, 3), Cost(), Need()(S.fr >= 1, "друзі")(S.phone, "телефон").s);
      add(A("Кіно", "200₴", A_MOOD, 4), U(200), Need()(S.cloth >= 1, "одяг із секонду").s);
      add(A("Футбол на стадіоні", "500₴", A_MOOD, 5), U(500), Need()(S.cloth >= 1, "одяг із секонду").s);
      add(A("Концерт", "$40", A_MOOD, 6), D(40), Need()(S.cloth >= 2, "нормальний одяг").s);
      add(A("Відпочинок у Карпатах", "$300", A_MOOD, 7, 7), D(300), Need()(S.docs, "документи").s);
      break;
    case 5:
      for (int j = 1; j < 4; j++)
        add(A(std::string(BAGS[j].n) + " (" + std::to_string(BAGS[j].cap) + " пл.)", fmt(BAGS[j].price) + "₴", A_BAG, j, 0), U(BAGS[j].price), "", S.bag >= j);
      add(A("Одяг із секонду", "300₴", A_ITEM, 0, 0), U(300), "", S.cloth >= 1);
      add(A("Нормальний одяг", "2 500₴", A_ITEM, 1, 0), U(2500), "", S.cloth >= 2);
      add(A("Діловий костюм", "$300", A_ITEM, 2, 0), D(300), "", S.cloth >= 3);
      add(A("Відновити документи", "800₴", A_ITEM, 3, 3), U(800), "", S.docs);
      add(A("Телефон б/в", "1 500₴", A_ITEM, 4, 0), U(1500), "", S.phone);
      add(A("Гітара б/в", "1 200₴", A_ITEM, 5, 0), U(1200), "", S.guitar);
      break;
    case 6:
      add(A("Читати в бібліотеці", "безк.", A_EDU, 0), Cost(), Need()(S.cloth >= 1, "одяг із секонду").s);
      add(A("Курси водіїв", "4 000₴", A_EDU, 1, 14), U(4000), Need()(S.docs, "документи").s, S.lic);
      add(A("Коледж", "15 000₴", A_EDU, 2, 30), U(15000), Need()(S.docs, "документи").s, S.edu >= 1);
      add(A("Університет", "$1 500", A_EDU, 3, 60), D(1500), Need()(S.edu >= 1, "коледж").s, S.edu >= 2);
      add(A("MBA", "$8 000", A_EDU, 4, 60), D(8000), Need()(S.edu >= 2, "університет")(S.cloth >= 3, "костюм").s, S.edu >= 3);
      break;
    case 7: levels(L, true); break;
    case 8: levels(L, false); break;
    case 9:
      for (int i = 0; i < 8; i++)
        add(A(std::string(JOBS[i].n) + (S.jx[i] ? " · " + std::to_string(S.jx[i]) + " дн." : ""), fc(jobPay(i)) + "/д", A_JOB, i), Cost(), jobNeed(i));
      break;
    case 10:
      for (int i = 0; i < 6; i++) {
        const Biz& b = BIZ[i];
        std::string inc = b.usd ? "$" + fmt(b.lo) + "–" + fmt(b.hi) : fmt(b.lo) + "–" + fmt(b.hi) + "₴";
        double cv = (b.cost.uah >= 0 ? b.cost.uah : b.cost.usd * S.rate) / ((b.lo + b.hi) / 2.0 * (b.usd ? S.rate : 1));
        Item it = A(b.n, fc(b.cost), A_BIZ, i, 0);
        it.ownedPrice = "+" + inc + "/д";
        it.desc = "Дохід " + inc + " на день." + (i == 0 ? " Щопонеділка продає твої пляшки оптом, склад на 1 000." : "") +
                  " Окупиться приблизно за " + fmtd(cv) + " дн." + (S.biz[i] ? " Вже твій." : "");
        add(it, b.cost, bizNeed(i), S.biz[i]);
      }
      if (S.biz[0]) taraRows(L);
      break;
    case 11:
      add(A("Роздавати листівки", "безк.", A_POL, 0), Cost(), Need()(S.docs, "документи").s);
      add(A("Допомагати бездомним", "500₴", A_POL, 1), U(500));
      add(A("Виступ на місцевому ТБ", "безк.", A_POL, 2), Cost(), Need()(S.pol >= 1, "бути активістом")(S.cloth >= 2, "нормальний одяг").s);
      add(A("Благодійний фонд", "$1 000", A_POL, 3), D(1000), Need()(S.pol >= 1, "бути активістом").s);
      if (S.pol < 5) {
        const Step& st = STEPS[S.pol];
        std::string need = Need()(S.rep >= st.rep, ("репутація " + std::to_string(st.rep)).c_str()).s;
        if (need.empty()) need = stepNeed(S.pol);
        add(A(st.n, fc(st.cost), A_STEP, 0, st.days), st.cost, need);
      } else {
        Item it = INFO("Ти — Президент", "✓"); it.cur = true; L.push_back(it);
      }
      break;
    case 12:
      add(A("Газовий балончик (є: " + std::to_string(S.spray) + ")", "150₴", A_SPRAY, 0, 0), U(150));
      add(A(S.dog ? std::string("Собака (") + (S.dogH ? "голодний!" : "ситий") + ")" : "Взяти собаку з притулку", "корм 20₴/д", A_DOG, 0, 0), Cost(), "", S.dog);
      add(A("Відкрити банківську картку", "200₴", A_CARD, 0, 0), U(200), Need()(S.docs, "документи").s, S.card);
      add(A("Покласти всю готівку на картку", fmt(S.uah) + "₴", A_DEPOSIT, 0, 0), Cost(), Need()(S.card, "картка")(S.uah > 0, "готівка").s);
      add(A("Зняти 500₴ з картки", "комісія 5₴", A_W500, 0, 0), Cost(), Need()(S.card, "картка")(S.bank >= 505, "505₴ на картці").s);
      add(A("Зняти все (" + fmt(S.bank) + "₴)", "комісія 1%", A_WALL, 0, 0), Cost(), Need()(S.card, "картка")(S.bank > 0, "гроші на картці").s);
      add(A(until(S.copUntil, "Домовитися з дільничним"), "300₴/тижд", A_COP, 0, 0), U(300));
      add(A("Курси самооборони", "1 500₴", A_SELFDEF, 0, 3), U(1500), "", S.selfdef);
      add(A(until(S.guardUntil, "Охорона бізнесу"), fmt(guardCost()) + "₴/міс", A_GUARD, 0, 0), U(guardCost()), Need()(bizN() > 0, "бізнес").s);
      add(A(until(S.lawUntil, "Юрист для бізнесу"), "5 000₴/міс", A_LAWYER, 0, 0), U(5000), Need()(bizN() > 0, "бізнес").s);
      break;
  }
  return L;
}

// ---------- days ----------
static void passDays(int n) {
  int p = PROT[S.house];
  S.food -= (int)r(4, 8);
  S.hp -= std::max<int>(0, (int)r(1, 4) - p);
  S.mood -= std::max<int>(0, (int)r(2, 6) - p);
  if (S.wxC < 0) { S.hp -= COLDHP[S.house]; if (S.house < 3) S.mood -= 2; S.food -= 2; }
  if (wet() && S.house < 2) { S.hp -= 2; S.mood -= 3; }
  if (S.wxT == 5 && S.house < 3) { S.hp -= 1; S.food -= 1; }
  for (int i = 0; i < n; i++) {
    S.day++;
    int y0 = S.age / 365;
    S.age++;
    if (S.age / 365 > y0) { S.uah += 200; S.mood += 20; note("З днем народження! Тобі " + std::to_string(S.age / 365) + ". Подарунок: +200₴"); }
    S.rate = (float)std::max(38.0, std::min(46.0, S.rate + (rnd() - .5) * .5));
    S.brate = (float)std::max(.8, std::min(2.0, S.brate + (rnd() - .5) * .15));
    const House& h = HOUSES[S.house];
    if (h.rent.any() && (!h.mon || mday(S.day) == 1)) {
      if (!payRent(h.rent)) { S.house = 0; note("Нема чим платити за житло. Тебе виселили на лавку."); }
    }
    int wd = weekday(S.day);
    if (S.biz[0] && wd == 1 && S.bot > 0) {
      int64_t b = S.bot, u = (int64_t)floor(b * S.brate * wm());
      S.uah += u; S.bot = 0;
      note("Понеділок: твій пункт прийому здав оптом " + fmt(b) + " пляшок за " + fmt(u) + "₴ (×" + f1(wm()) + ").");
    }
    if (S.biz[0]) {
      if (S.workers > 0) {
        if (payRent(U(100 * S.workers))) {
          int64_t got = 0;
          for (int w = 0; w < S.workers; w++) got += r(20, 40);
          putBot(got);
          if (ch(.01 * S.workers)) { int64_t l = std::min<int64_t>(S.bot, r(1, 10)); S.bot -= l; note("Один зі збирачів поцупив " + fmt(l) + " пляшок зі складу."); }
          if (ch(.02 * S.workers)) { S.workers--; note("Один збирач пішов від тебе. Лишилось: " + std::to_string(S.workers) + "."); }
        } else { S.workers = 0; note("Нема чим платити збирачам — усі розбіглися."); }
      }
      if (S.day < S.contrUntil && (wd == 0 || wd == 6)) putBot(r(80, 200));
      if (S.comp && S.day >= S.comp) { S.comp = 0; note("Конкурентний пункт прийому закрився. Ціни знову твої!"); }
    }
    for (int b = 0; b < 6; b++) {
      if (!S.biz[b]) continue;
      const Biz& z = BIZ[b];
      int64_t v = llround(r(z.lo, z.hi) * (b == 0 && S.comp ? .6 : 1));
      if (z.usd) S.usd += v; else S.uah += v;
      if (ch(.03)) {
        int k = (int)r(0, 2);
        bool cu = z.cost.uah >= 0;
        static const double PCT[3] = {.03, .05, .02};
        int64_t amt = llround((cu ? z.cost.uah : z.cost.usd) * PCT[k]);
        if (k == 1 && S.day < S.guardUntil) note(std::string(z.n) + ": охорона відбила пограбування!");
        else {
          std::string ex;
          if (k == 0 && S.day < S.lawUntil) { amt = llround(amt / 2.0); ex = " (юрист зменшив штраф удвічі)"; }
          if (cu) S.uah -= amt; else S.usd -= amt;
          static const char* WHAT[3] = {"податкова перевірка, штраф", "пограбування", "зламалось обладнання"};
          note(std::string(z.n) + ": " + WHAT[k] + " −" + (cu ? fmt(amt) + "₴" : "$" + fmt(amt)) + ex);
        }
      }
    }
    if (S.dog) {
      if (S.uah >= 20) { S.uah -= 20; S.dogH = 0; S.mood += 1; }
      else if (++S.dogH >= 3) { S.dog = 0; S.dogH = 0; note("Собаку три дні не було чим годувати, і він пішов від тебе."); }
    }
    S.mood += TRM[S.trans];
    if (!S.pv && S.fr > 0 && S.day - S.lastFr > 14 && (S.day - S.lastFr) % 7 == 1) {
      S.fr--; note("Ти давно не бачився з друзями, і один віддалився. Друзів: " + std::to_string(S.fr) + ". Зустрічайся або дзвони їм.");
    }
    if (POL_PAY[S.pol]) S.usd += POL_PAY[S.pol];
    if (S.pol >= 2) S.rep += 1;
    if (!S.pv) {
      int k = 0;
      for (int j = 0; j < S.nloans; j++) {
        Loan l = S.loans[j];
        if (S.day < l.due) { S.loans[k++] = l; continue; }
        if (l.back) { S.uah += llround(l.amt * 1.1); note("Друг повернув борг з подякою: +" + fmtd(l.amt * 1.1) + "₴"); }
        else { if (S.fr > 0) S.fr--; S.mood -= 10; note("Друг так і не віддав " + fmt(l.amt) + "₴ і зник. Друзів: " + std::to_string(S.fr)); }
      }
      S.nloans = k;
    }
    genWx();
  }
}

static void die(const std::string& why) {
  S.dead = 1;
  snprintf(S.deadWhy, sizeof S.deadWhy, "%s", why.c_str());
  S.best = std::max(S.best, S.day);
}

static void check(bool dayPassed) {
  clamp();
  static const char* NM[3] = {"здоровʼя", "ситість", "настрій"};
  int* V[3] = {&S.hp, &S.food, &S.mood};
  bool z[3];
  int nz = 0;
  for (int i = 0; i < 3; i++) { z[i] = *V[i] <= 0; nz += z[i]; }
  if (nz && dayPassed && S.fr > 0 && rnd() < std::min(.6, .15 * S.fr)) {
    std::string got;
    for (int i = 0; i < 3; i++) if (z[i]) { int v = (int)r(5, 10); *V[i] = v; if (!got.empty()) got += ", "; got += std::string(NM[i]) + " +" + std::to_string(v); z[i] = false; }
    note("Друзі не дали тобі пропасти: " + got + ".");
    S.lastFr = S.day;
    nz = 0;
  }
  if (nz) {
    std::string names;
    for (int i = 0; i < 3; i++) if (z[i]) { if (!names.empty()) names += ", "; names += NM[i]; }
    if (dayPassed) S.crit++;
    if (S.crit > 7) { die("Сім днів на межі: " + names + "."); return; }
    msg += " ⚠ " + names + " на нулі! Лишилось " + std::to_string(8 - S.crit) + " дн.";
  } else S.crit = 0;
  if (S.age >= 85 * 365) die("Помер від старості у 85.");
}

static void flush() {
  if (!ask.on && popup.empty() && !notes.empty()) {
    for (size_t i = 0; i < notes.size(); i++) { if (i) popup += "\n\n"; popup += notes[i]; }
    notes.clear();
  }
}

// ---------- events ----------
static void setAsk(const std::string& t, const char* a, const char* b, int kind, int64_t amt) {
  ask.on = true; ask.text = t; ask.opt[0] = a; ask.opt[1] = b; ask.kind = kind; ask.sel = 0; ask.amt = amt;
}
static const char* ASKED = "\x01";
static std::string event(int k) {
  switch (k) {
    case 0: { int64_t u = r(100, 600); S.uah += u; return "Знайшов гаманець без документів. +" + fmt(u) + "₴"; }
    case 1: if (S.house < 2 && wet()) { S.hp -= 12; return std::string(S.wxT == 0 ? "Снігопад" : "Злива") + ". Змок до нитки.\nЗдоровʼя −12"; } return "";
    case 2: if (S.house == 0 && !copOk() && rnd() < copF()) { S.mood -= 10; return "Поліція прогнала з лавки.\nНастрій −10"; } return "";
    case 3: S.food += 30; return "Волонтери пригостили гарячим обідом.\nСитість +30";
    case 4: if (S.bot > 0) { std::string s = saved(); if (!s.empty()) return s; S.bot = 0; return "Хтось поцупив усі твої пляшки!"; } return "";
    case 5: if (!S.phone) { S.phone = 1; return "Знайшов на смітнику робочий телефон!"; } return "";
    case 6:
      if (S.uah > 300) {
        std::string s = saved(); if (!s.empty()) return s;
        int64_t l = (int64_t)floor(S.uah * .3); S.uah -= l;
        return "Кишенькові злодії вкрали готівку: −" + fmt(l) + "₴" + (S.card ? "" : "\nГроші на картці були б у безпеці.");
      }
      return "";
    case 7: S.mood += 15; return "Старий знайомий упізнав тебе і пригостив кавою.\nНастрій +15";
    case 8: if (S.fr > 0) { int64_t u = r(50, 300); S.uah += u; return "Друг підкинув трохи грошей.\n+" + fmt(u) + "₴"; } return "";
    case 9: if (S.fr > 0) { int v = (int)r(5, 10); S.food += v; return "Друг поділився обідом.\nСитість +" + std::to_string(v); } return "";
    case 10: if (S.biz[0] && !S.comp) { S.comp = S.day + (int)r(30, 60); return "Поруч відкрився конкурентний пункт прийому!\nОптова ціна й дохід пункту впали.\nДивись вкладку «Бізнес»."; } return "";
    case 11:
      if (S.fr > 0) { int64_t b = std::min<int64_t>(r(5, 20), cap() - S.bot); if (b <= 0) return ""; S.bot += b; return "Друг приніс пляшки, які знайшов.\n+" + fmt(b) + " пляшок"; }
      return "";
    case 12: if (S.fr > 0) { int64_t g = r(3, 8) * 100; setAsk("Друг кличе на підробіток увечері:\nрозвантажити фуру за " + fmt(g) + "₴.", "Піти", "Відмовитись", 1, g); return ASKED; } return "";
    case 13: if (S.fr > 0 && S.house < 4) { setAsk("Друг знає вільне місце в гуртожитку.\nДо кінця місяця безкоштовно, документи не потрібні.", "Заселитися", "Ні", 2, 0); return ASKED; } return "";
    case 14:
      if (S.fr > 0 && S.uah >= 150) { int64_t a = std::min<int64_t>(S.uah, r(3, 10) * 50); setAsk("Друг просить позичити " + fmt(a) + "₴.\nОбіцяє віддати за тиждень-два.", "Позичити", "Відмовити", 3, a); return ASKED; }
      return "";
  }
  return "";
}
static const int NEVENTS = 15;

static void resolveAsk() {
  ask.on = false;
  if (ask.kind == 1) {
    if (ask.sel == 0) { S.uah += ask.amt; S.food -= 10; S.hp -= 3; S.lastFr = S.day; clamp(); msg = "Розвантажив фуру разом з другом: +" + fmt(ask.amt) + "₴."; }
    else msg = "Відмовився від підробітку.";
  } else if (ask.kind == 2) {
    if (ask.sel == 0) { S.house = 4; S.lastFr = S.day; msg = "Заселився в гуртожиток. До " + dd(S.day + daysToRent()) + " безкоштовно, далі 4 000₴/міс."; }
    else msg = "Лишився там, де був.";
  } else if (ask.kind == 3) {
    if (ask.sel == 0) {
      S.uah -= ask.amt;
      if (S.nloans < 8) S.loans[S.nloans++] = {(int)ask.amt, S.day + (int)r(5, 14), (uint8_t)(rnd() < .7)};
      S.rep += 3;
      msg = "Позичив другу " + fmt(ask.amt) + "₴. Репутація +3.";
    } else {
      S.rep = std::max(0, S.rep - 3);
      if (rnd() < .4 && S.fr > 0) { S.fr--; msg = "Друг образився і розповів усім. Друзів: " + std::to_string(S.fr) + ", репутація −3."; }
      else msg = "Відмовив. Друг зрозумів, але пішов поголос. Репутація −3.";
    }
  }
}

// ---------- game flow ----------
void newGame(int best) {
  initTables();
  memset(&S, 0, sizeof S);
  S.magic = SAVE_MAGIC;
  S.day = 1; S.age = 25 * 365; S.hp = 70; S.food = 60; S.mood = 50; S.uah = 50;
  S.lastFr = 1; S.wh = 1000; S.rate = 41.5f; S.brate = 1.2f; S.best = best;
  S.wxT = 4; S.wxC = 18;
  ui = UIState();
  ask = Ask();
  popup.clear(); notes.clear();
  msg = "1 вересня. Ти прокинувся на лавці в парку. Головне — вижити.";
}

void loadedState() {
  initTables();
  S.pv = 0; S.lost = 0; S.half = 0;
  msg = "Гру продовжено. День " + fmt(S.day) + ".";
}

static void ok() {
  if (S.dead) { newGame(S.best); return; }
  if (ask.on) { resolveAsk(); flush(); return; }
  if (!popup.empty()) { popup.clear(); flush(); return; }
  std::vector<Item> L = acts(ui.tab);
  if (ui.cur[ui.tab] >= (int)L.size()) return;
  const Item& it = L[ui.cur[ui.tab]];
  if (it.kind == K_RESET) {
    if (ui.confirm) { newGame(std::max(S.best, S.day)); return; }
    ui.confirm = true; msg = "Натисни OK ще раз, щоб почати спочатку."; return;
  }
  ui.confirm = false;
  if (it.kind == K_INFO) return;
  if (it.kind == K_LOCKED) { msg = "Не можна: " + it.locked + "."; return; }
  if (it.owned) { msg = "Це в тебе вже є."; return; }
  if (!it.need.empty()) { msg = "Потрібно: " + it.need + "."; return; }
  if (!canPay(it.cost)) { msg = "Не вистачає грошей."; return; }
  pay(it.cost);
  msg = run(it.id, it.arg);
  if (it.days > 0) {
    passDays(it.days);
    if (notes.empty() && rnd() < .13) {
      for (int k = 0; k < 8; k++) {
        std::string e = event((int)r(0, NEVENTS - 1));
        if (!e.empty()) { if (e != ASKED) note(e); break; }
      }
    }
  }
  check(it.days > 0);
  flush();
}

void key(int k) {
  if (k == K_BACK) {                         // «Назад»: закрити вікно / відмовитись / на вкладку «Стан»
    if (S.dead) return;
    if (!popup.empty()) { popup.clear(); flush(); return; }
    if (ask.on) { ask.sel = 1; resolveAsk(); flush(); return; }
    if (ui.confirm) { ui.confirm = false; msg = "Скасовано."; return; }
    ui.tab = 0;
    return;
  }
  if (S.dead || !popup.empty()) { if (k == K_OK) ok(); return; }
  if (ask.on) { if (k == K_OK) ok(); else ask.sel ^= 1; return; }
  int n = (int)acts(ui.tab).size();
  int& c = ui.cur[ui.tab];
  if (k == K_UP) c = (c - 1 + n) % n;
  if (k == K_DOWN) c = (c + 1) % n;
  if (k == K_LEFT) { ui.tab = (ui.tab + NTABS - 1) % NTABS; ui.confirm = false; }
  if (k == K_RIGHT) { ui.tab = (ui.tab + 1) % NTABS; ui.confirm = false; }
  if (k == K_OK) ok();
}

bool preview(const Item& it, int64_t d[7][2]) {
  if (it.kind != K_ACT || it.owned || !it.id) return false;
  static State real;
  real = S;
  int64_t v[2][7];
  for (int q = 0; q < 2; q++) {
    S = real; S.pv = 1; rmode = q + 1;
    pay(it.cost);
    run(it.id, it.arg);
    if (it.days > 0) passDays(it.days);
    int64_t now[7] = {S.hp, S.food, S.mood, S.uah, S.usd, S.bot, S.rep};
    int64_t was[7] = {real.hp, real.food, real.mood, real.uah, real.usd, real.bot, real.rep};
    for (int k = 0; k < 7; k++) v[q][k] = now[k] - was[k];
  }
  S = real; rmode = 0;
  bool any = false;
  for (int k = 0; k < 7; k++) {
    d[k][0] = std::min(v[0][k], v[1][k]);
    d[k][1] = std::max(v[0][k], v[1][k]);
    any |= d[k][0] || d[k][1];
  }
  return any;
}
