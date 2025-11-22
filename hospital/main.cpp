// Enhanced Ambulance Fleet System
// Build: g++ -std=c++17 main.cpp -o main -lraylib -lm -lpthread -ldl -lrt -lX11

#include "raylib.h"
#include <vector>
#include <string>
#include <random>
#include <ctime>
#include <cmath>
#include <queue>
#include <algorithm>
#include <iostream>
#include <optional>
#include <limits>
#include <deque>

using namespace std;

// ----------------------------- Types ---------------------------------------

struct House
{
    Rectangle body;
    Color color;
    int id;
    bool highlighted = false;
    bool hasEmergency = false;
};

struct Road
{
    Rectangle rect;
    bool horizontal;
};

struct Ambulance
{
    int id = 0;
    Vector2 pos = {0, 0};
    Vector2 parkingPos = {0, 0};
    float speed = 150.0f;
    Color color = RED;
    vector<Vector2> path;
    int currentPathIndex = 0;
    bool busy = false;
    int assignedEmergencyId = -1;
    string assignedPatientName = "";
    int assignedHouseId = -1;
    
    enum class Status
    {
        IDLE,
        TO_SCENE,
        ON_SCENE,
        RETURNING
    } status = Status::IDLE;
    float onSceneTimer = 0.0f;
    Rectangle bounds() const { return Rectangle{pos.x - 10, pos.y - 8, 20, 16}; }

    string getStatusString() const
    {
        switch (status)
        {
        case Status::IDLE:
            return "IDLE";
        case Status::TO_SCENE:
            return "EN ROUTE";
        case Status::ON_SCENE:
            return "ON SCENE";
        case Status::RETURNING:
            return "RETURNING";
        default:
            return "UNKNOWN";
        }
    }
};

struct PatientInfo
{
    string name;
    int age = 0;
    string severity;
    string desc;
    int houseNumber = -1;
};

struct Emergency
{
    int id = 0;
    PatientInfo patient;
    Vector2 location = {0, 0};
    int priority = 3;
    double createdAt = 0.0;
    int assignedHospital = -1;
};

struct EmergencyCompare
{
    bool operator()(const Emergency &a, const Emergency &b) const
    {
        if (a.priority != b.priority)
            return a.priority > b.priority;
        return a.createdAt > b.createdAt;
    }
};

struct EmergencyLog
{
    string message;
    double timestamp;
    Color color;
};

// ----------------------------- Pathfinding --------------------------------

Vector2 findNearestRoadPoint(Vector2 target, float startX, float startY, float blockSize, int blocksX, int blocksY)
{
    float minDist = 1e9f;
    Vector2 nearest = target;
    for (int y = 0; y <= blocksY; ++y)
        for (int x = 0; x <= blocksX; ++x)
        {
            Vector2 inter = {startX + x * blockSize, startY + y * blockSize};
            float dx = target.x - inter.x, dy = target.y - inter.y;
            float d = sqrtf(dx * dx + dy * dy);
            if (d < minDist)
            {
                minDist = d;
                nearest = inter;
            }
        }
    return nearest;
}

vector<Vector2> findPathOnRoads(Vector2 start, Vector2 end, float startX, float startY, float blockSize, int blocksX, int blocksY)
{
    vector<Vector2> path;
    Vector2 s = findNearestRoadPoint(start, startX, startY, blockSize, blocksX, blocksY);
    Vector2 e = findNearestRoadPoint(end, startX, startY, blockSize, blocksX, blocksY);
    path.push_back(s);
    Vector2 cur = s;
    while (fabsf(cur.x - e.x) > 1.0f || fabsf(cur.y - e.y) > 1.0f)
    {
        if (fabsf(cur.x - e.x) > 1.0f)
            cur.x += (cur.x < e.x) ? blockSize : -blockSize;
        else if (fabsf(cur.y - e.y) > 1.0f)
            cur.y += (cur.y < e.y) ? blockSize : -blockSize;
        path.push_back(cur);
    }
    path.push_back(end);
    return path;
}

// ----------------------------- Hospital -----------------------------------

class Hospital
{
public:
    Hospital(Vector2 loc, const vector<Vector2> &parkingPositions, int startAmbId = 1, float onSceneDuration = 4.0f)
        : location(loc), nextEmergencyId(1), onSceneDurationSec(onSceneDuration)
    {
        int aid = startAmbId;
        for (auto &p : parkingPositions)
        {
            Ambulance a;
            a.id = aid++;
            a.parkingPos = p;
            a.pos = p;
            a.color = Color{(unsigned char)((a.id * 47) % 200 + 30), (unsigned char)((a.id * 31) % 200 + 30), (unsigned char)((a.id * 19) % 200 + 30), 255};
            a.status = Ambulance::Status::IDLE;
            ambulances.push_back(a);
        }
    }

    void receiveEmergency(const Emergency &incoming)
    {
        Emergency e = incoming;
        e.id = nextEmergencyId++;
        e.createdAt = GetTime();
        queue_.push(e);
    }

    void dispatchVehicles(float startX, float startY, float blockSize, int blocksX, int blocksY)
    {
        if (queue_.empty())
            return;
        vector<Emergency> backlog;
        while (!queue_.empty())
        {
            Emergency em = queue_.top();
            queue_.pop();
            int idx = findNearestAvailableAmbulance(em.location);
            if (idx >= 0)
            {
                Ambulance &amb = ambulances[idx];
                amb.path = findPathOnRoads(amb.pos, em.location, startX, startY, blockSize, blocksX, blocksY);
                amb.currentPathIndex = 0;
                amb.busy = true;
                amb.assignedEmergencyId = em.id;
                amb.assignedPatientName = em.patient.name;
                amb.assignedHouseId = em.patient.houseNumber;
                amb.status = Ambulance::Status::TO_SCENE;
                amb.onSceneTimer = 0.0f;
            }
            else
                backlog.push_back(em);
        }
        for (auto &e : backlog)
            queue_.push(e);
    }

    void updateAfterMovement(float startX, float startY, float blockSize, int blocksX, int blocksY)
    {
        for (auto &amb : ambulances)
        {
            if (amb.status == Ambulance::Status::TO_SCENE)
            {
                if (amb.currentPathIndex >= (int)amb.path.size())
                {
                    amb.status = Ambulance::Status::ON_SCENE;
                    amb.onSceneTimer = onSceneDurationSec;
                }
                else
                {
                    Vector2 targ = amb.path.back();
                    float dx = targ.x - amb.pos.x, dy = targ.y - amb.pos.y;
                    if (sqrtf(dx * dx + dy * dy) < 4.0f)
                    {
                        amb.currentPathIndex = (int)amb.path.size();
                        amb.status = Ambulance::Status::ON_SCENE;
                        amb.onSceneTimer = onSceneDurationSec;
                    }
                }
            }
            else if (amb.status == Ambulance::Status::ON_SCENE)
            {
                amb.onSceneTimer -= GetFrameTime();
                if (amb.onSceneTimer <= 0.0f)
                {
                    amb.path = findPathOnRoads(amb.pos, amb.parkingPos, startX, startY, blockSize, blocksX, blocksY);
                    amb.currentPathIndex = 0;
                    amb.status = Ambulance::Status::RETURNING;
                    handledCount++;
                    amb.assignedEmergencyId = -1;
                    amb.assignedPatientName = "";
                    amb.assignedHouseId = -1;
                }
            }
            else if (amb.status == Ambulance::Status::RETURNING)
            {
                Vector2 targ = amb.parkingPos;
                float dx = targ.x - amb.pos.x, dy = targ.y - amb.pos.y;
                if (sqrtf(dx * dx + dy * dy) < 4.0f || amb.currentPathIndex >= (int)amb.path.size())
                {
                    amb.pos = amb.parkingPos;
                    amb.path.clear();
                    amb.currentPathIndex = 0;
                    amb.status = Ambulance::Status::IDLE;
                    amb.busy = false;
                }
            }
        }
    }

    vector<Emergency> peekAllPending() const
    {
        vector<Emergency> out;
        auto copy = queue_;
        while (!copy.empty())
        {
            out.push_back(copy.top());
            copy.pop();
        }
        return out;
    }

    vector<Ambulance> &getAmbulances() { return ambulances; }
    int pendingCount() const { return (int)queue_.size(); }
    int handled() const { return handledCount; }
    Vector2 getLocation() const { return location; }

private:
    Vector2 location;
    vector<Ambulance> ambulances;
    priority_queue<Emergency, vector<Emergency>, EmergencyCompare> queue_;
    int nextEmergencyId;
    int handledCount = 0;
    float onSceneDurationSec;

    int findNearestAvailableAmbulance(const Vector2 &target)
    {
        int best = -1;
        float bestd = numeric_limits<float>::max();
        for (size_t i = 0; i < ambulances.size(); ++i)
        {
            auto &a = ambulances[i];
            if (a.status == Ambulance::Status::IDLE && !a.busy)
            {
                float dx = a.pos.x - target.x, dy = a.pos.y - target.y;
                float d = sqrtf(dx * dx + dy * dy);
                if (d < bestd)
                {
                    bestd = d;
                    best = (int)i;
                }
            }
        }
        return best;
    }
};

// ----------------------------- UI helpers ----------------------------------

struct TextField
{
    Rectangle r;
    string text;
    bool active = false;
    int maxLen = 64;
    string errorMsg = "";

    void draw(const char *label) const
    {
        Color bgColor = active ? Fade(WHITE, 0.98f) : Fade(WHITE, 0.9f);
        if (!errorMsg.empty())
            bgColor = Fade(RED, 0.1f);

        DrawRectangleRec(r, bgColor);
        DrawRectangleLinesEx(r, 1, errorMsg.empty() ? GRAY : RED);
        DrawText(label, (int)r.x + 6, (int)r.y - 18, 12, DARKGRAY);
        DrawText(text.c_str(), (int)r.x + 6, (int)r.y + 6, 14, BLACK);
        if (active)
        {
            int tw = MeasureText(text.c_str(), 14);
            DrawRectangle((int)(r.x + 6 + tw), (int)(r.y + 6), 2, 16, BLACK);
        }
        if (!errorMsg.empty())
        {
            DrawText(errorMsg.c_str(), (int)r.x + 6, (int)(r.y + r.height + 4), 10, RED);
        }
    }
};

// ----------------------------- Main ---------------------------------------

int main()
{
    const int screenW = 1600, screenH = 900;
    InitWindow(screenW, screenH, "Enhanced Ambulance Fleet System");
    SetTargetFPS(60);

    // map params
    const int blocksX = 3, blocksY = 3;
    const float blockSize = 200.0f;
    const float roadW = 44.0f, sidewalk = 10.0f;
    const float sideMargin = 50.0f, topMargin = 50.0f;
    float mapWidth = blocksX * blockSize + roadW * 2;
    float mapHeight = blocksY * blockSize + roadW * 2;
    float startX = sideMargin + 50;
    float startY = topMargin + 50;
    float offsetX = 0, offsetY = 0;

    // roads
    vector<Road> roads;
    for (int y = 0; y <= blocksY; ++y)
    {
        float ry = startY + y * blockSize;
        roads.push_back({Rectangle{startX - roadW / 2.0f, ry - roadW / 2.0f, mapWidth + roadW, roadW}, true});
    }
    for (int x = 0; x <= blocksX; ++x)
    {
        float rx = startX + x * blockSize;
        roads.push_back({Rectangle{rx - roadW / 2.0f, startY - roadW / 2.0f, roadW, mapHeight + roadW}, false});
    }

    // houses
    vector<House> houses;
    const int lotsX = 3, lotsY = 2;
    int houseId = 1;
    mt19937 rng((unsigned)time(NULL));
    auto rndf = [&](float a, float b)
    { uniform_real_distribution<float>d(a,b); return d(rng); };
    auto rndi = [&](int a, int b)
    { uniform_int_distribution<int>d(a,b); return d(rng); };
    float pad = 8.0f;
    for (int py = 0; py < blocksY * lotsY; ++py)
    {
        for (int px = 0; px < blocksX * lotsX; ++px)
        {
            int by = py / lotsY, ly = py % lotsY;
            int bx = px / lotsX, lx = px % lotsX;
            float blockX = startX + bx * blockSize + roadW / 2.0f;
            float blockY = startY + by * blockSize + roadW / 2.0f;
            float usableW = blockSize - roadW, usableH = blockSize - roadW;
            float lotW = usableW / lotsX, lotH = usableH / lotsY;
            float x = blockX + lx * lotW + pad / 2.0f, y = blockY + ly * lotH + pad / 2.0f;
            float w = lotW - pad, h = lotH - pad;
            float vw = w * rndf(0.75f, 0.95f), vh = h * rndf(0.55f, 0.85f);
            Rectangle body = {x + (w - vw) / 2.0f, y + (h - vh) / 2.0f + vh * 0.08f, vw, vh};
            houses.push_back({body, Color{(unsigned char)rndi(60, 220), (unsigned char)rndi(60, 220), (unsigned char)rndi(60, 220), 255}, houseId++, false, false});
        }
    }

   // create single hospital (centered at top)
vector<Hospital> hospitals;
float hospCenterX = startX + mapWidth / 2.0f;
float hospY = startY - 100;
vector<Vector2> parking = {
    {hospCenterX - 70, hospY + 30},  // Left side parking
    {hospCenterX - 35, hospY + 30},
    {hospCenterX + 35, hospY + 30},
    {hospCenterX + 70, hospY + 30}   // Right side parking
};
hospitals.emplace_back(Vector2{hospCenterX, hospY}, parking, 1, 4.0f);

    // UI Panels
    Rectangle formPanel = {screenW - 340.0f, 60.0f, 320.0f, 480.0f};
    TextField tfName{{formPanel.x + 12, formPanel.y + 40, formPanel.width - 24, 28}, "", false, 32};
    TextField tfAge{{formPanel.x + 12, formPanel.y + 90, formPanel.width - 24, 28}, "", false, 4};
    vector<string> severities = {"Normal", "High", "Critical"};
    int severityIdx = 0;
    TextField tfDesc{{formPanel.x + 12, formPanel.y + 190, formPanel.width - 24, 60}, "", false, 200};
    TextField tfHouse{{formPanel.x + 12, formPanel.y + 270, formPanel.width - 24, 28}, "", false, 6};
    Rectangle btnSubmit = {formPanel.x + 12, formPanel.y + 320, 140, 35};
    Rectangle btnClear = {formPanel.x + 168, formPanel.y + 320, 140, 35};

    // Emergency Queue Panel
    Rectangle queuePanel = {screenW - 340.0f, formPanel.y + formPanel.height + 20, 320.0f, 240.0f};

    // Activity Log
    deque<EmergencyLog> activityLog;
    int activeField = -1, hoverHouse = -1;
    double gameTime = 0.0;
    int totalEmergencies = 0;
    bool hoverHospital = false;

    while (!WindowShouldClose())
    {
        float dt = GetFrameTime();
        gameTime += dt;

        // pan
        float panSpeed = 240.0f;
        if (IsKeyDown(KEY_RIGHT))
            offsetX -= panSpeed * dt;
        if (IsKeyDown(KEY_LEFT))
            offsetX += panSpeed * dt;
        if (IsKeyDown(KEY_DOWN))
            offsetY -= panSpeed * dt;
        if (IsKeyDown(KEY_UP))
            offsetY += panSpeed * dt;

        // Update house emergency status
        for (auto &h : houses)
            h.hasEmergency = false;
        for (auto &hosp : hospitals)
        {
            for (auto &amb : hosp.getAmbulances())
            {
                if (amb.status == Ambulance::Status::TO_SCENE || amb.status == Ambulance::Status::ON_SCENE)
                {
                    for (auto &h : houses)
                    {
                        Rectangle hb = h.body;
                        Vector2 center = {hb.x + hb.width / 2, hb.y + hb.height};
                        if (!amb.path.empty())
                        {
                            Vector2 target = amb.path.back();
                            if (fabsf(center.x - target.x) < 5 && fabsf(center.y - target.y) < 5)
                            {
                                h.hasEmergency = true;
                            }
                        }
                    }
                }
            }
        }

        // update ambulances
        for (auto &h : hospitals)
        {
            for (auto &amb : h.getAmbulances())
            {
                if (!amb.path.empty() && amb.currentPathIndex < (int)amb.path.size())
                {
                    Vector2 t = amb.path[amb.currentPathIndex];
                    Vector2 d = {t.x - amb.pos.x, t.y - amb.pos.y};
                    float dist = sqrtf(d.x * d.x + d.y * d.y);
                    if (dist > 3.0f)
                    {
                        d.x /= dist;
                        d.y /= dist;
                        float sp = amb.speed;
                        if (amb.status == Ambulance::Status::RETURNING)
                            sp *= 0.8f;
                        amb.pos.x += d.x * sp * dt;
                        amb.pos.y += d.y * sp * dt;
                    }
                    else
                        amb.currentPathIndex++;
                }
                else
                {
                    if (amb.status == Ambulance::Status::IDLE)
                    {
                        Vector2 tgt = amb.parkingPos;
                        float dx = tgt.x - amb.pos.x, dy = tgt.y - amb.pos.y, dist = sqrtf(dx * dx + dy * dy);
                        if (dist > 1.0f)
                        {
                            amb.pos.x += (dx / dist) * amb.speed * 0.4f * dt;
                            amb.pos.y += (dy / dist) * amb.speed * 0.4f * dt;
                        }
                    }
                }
            }
            h.dispatchVehicles(startX, startY, blockSize, blocksX, blocksY);
            h.updateAfterMovement(startX, startY, blockSize, blocksX, blocksY);
        }

        // input handling
        Vector2 mouse = GetMousePosition();
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            tfName.errorMsg = tfAge.errorMsg = tfHouse.errorMsg = "";

            if (CheckCollisionPointRec(mouse, tfName.r))
            {
                activeField = 0;
                tfName.active = true;
                tfAge.active = tfDesc.active = tfHouse.active = false;
            }
            else if (CheckCollisionPointRec(mouse, tfAge.r))
            {
                activeField = 1;
                tfAge.active = true;
                tfName.active = tfDesc.active = tfHouse.active = false;
            }
            else if (CheckCollisionPointRec(mouse, tfDesc.r))
            {
                activeField = 2;
                tfDesc.active = true;
                tfName.active = tfAge.active = tfHouse.active = false;
            }
            else if (CheckCollisionPointRec(mouse, tfHouse.r))
            {
                activeField = 3;
                tfHouse.active = true;
                tfName.active = tfAge.active = tfDesc.active = false;
            }
            else
            {
                activeField = -1;
                tfName.active = tfAge.active = tfDesc.active = tfHouse.active = false;
            }

            Rectangle sevRect = {formPanel.x + 12, formPanel.y + 140, formPanel.width - 24, 28};
            if (CheckCollisionPointRec(mouse, sevRect))
                severityIdx = (severityIdx + 1) % (int)severities.size();

            if (CheckCollisionPointRec(mouse, btnSubmit))
            {
                bool valid = true;

                if (tfName.text.empty())
                {
                    tfName.errorMsg = "Name required";
                    valid = false;
                }

                if (tfAge.text.empty())
                {
                    tfAge.errorMsg = "Age required";
                    valid = false;
                }

                int houseNum = -1;
                try
                {
                    if (!tfHouse.text.empty())
                        houseNum = stoi(tfHouse.text);
                }
                catch (...)
                {
                    tfHouse.errorMsg = "Invalid number";
                    valid = false;
                }

                int found = -1;
                for (size_t i = 0; i < houses.size(); ++i)
                    if (houses[i].id == houseNum)
                    {
                        found = (int)i;
                        break;
                    }

                if (found == -1)
                {
                    tfHouse.errorMsg = "House not found";
                    valid = false;
                }

                if (valid)
                {
                    Emergency em;
                    em.patient.name = tfName.text;
                    try
                    {
                        if (!tfAge.text.empty())
                            em.patient.age = stoi(tfAge.text);
                    }
                    catch (...)
                    {
                    }
                    em.patient.severity = severities[severityIdx];
                    em.patient.desc = tfDesc.text;
                    em.patient.houseNumber = houseNum;
                    Rectangle hb = houses[found].body;
                    em.location = {hb.x + hb.width / 2.0f, hb.y + hb.height};
                    em.priority = (severities[severityIdx] == "Critical") ? 1 : (severities[severityIdx] == "High" ? 2 : 3);

                    // Assign to the single hospital
                    em.assignedHospital = 0;
                    hospitals[0].receiveEmergency(em);
                    totalEmergencies++;

                    // Add to log
                    EmergencyLog log;
                    log.message = tfName.text + " (" + severities[severityIdx] + ") - Hospital";
                    log.timestamp = gameTime;
                    log.color = (severityIdx == 2) ? RED : (severityIdx == 1 ? ORANGE : BLUE);
                    activityLog.push_front(log);
                    if (activityLog.size() > 8)
                        activityLog.pop_back();

                    tfName.text.clear();
                    tfAge.text.clear();
                    tfDesc.text.clear();
                    tfHouse.text.clear();
                }
            }
            if (CheckCollisionPointRec(mouse, btnClear))
            {
                tfName.text.clear();
                tfAge.text.clear();
                tfDesc.text.clear();
                tfHouse.text.clear();
                tfName.errorMsg = tfAge.errorMsg = tfHouse.errorMsg = "";
            }
        }

        int key = 0;
        while ((key = GetCharPressed()) > 0)
        {
            if (activeField == 0 && (int)tfName.text.size() < tfName.maxLen)
                tfName.text.push_back((char)key);
            else if (activeField == 1 && (int)tfAge.text.size() < tfAge.maxLen)
            {
                char c = (char)key;
                if (c >= '0' && c <= '9')
                    tfAge.text.push_back(c);
            }
            else if (activeField == 2 && (int)tfDesc.text.size() < tfDesc.maxLen)
                tfDesc.text.push_back((char)key);
            else if (activeField == 3 && (int)tfHouse.text.size() < tfHouse.maxLen)
            {
                char c = (char)key;
                if (c >= '0' && c <= '9')
                    tfHouse.text.push_back(c);
            }
        }
        if (IsKeyPressed(KEY_BACKSPACE))
        {
            if (activeField == 0 && !tfName.text.empty())
                tfName.text.pop_back();
            if (activeField == 1 && !tfAge.text.empty())
                tfAge.text.pop_back();
            if (activeField == 2 && !tfDesc.text.empty())
                tfDesc.text.pop_back();
            if (activeField == 3 && !tfHouse.text.empty())
                tfHouse.text.pop_back();
        }
        if (IsKeyPressed(KEY_TAB))
        {
            activeField = (activeField + 1) % 4;
            tfName.active = (activeField == 0);
            tfAge.active = (activeField == 1);
            tfDesc.active = (activeField == 2);
            tfHouse.active = (activeField == 3);
        }

        // hover house id
        hoverHouse = -1;
        Vector2 mouseWorld = {GetMouseX() - offsetX, GetMouseY() - offsetY};
        for (auto &h : houses)
            if (CheckCollisionPointRec(mouseWorld, h.body))
            {
                hoverHouse = h.id;
                break;
            }

        // Check if hovering over hospital
        hoverHospital = false;
        if (!hospitals.empty())
        {
            Vector2 hospLoc = hospitals[0].getLocation();
            Vector2 hospScreen = {hospLoc.x + offsetX, hospLoc.y + offsetY};
            float dx = mouse.x - hospScreen.x;
            float dy = mouse.y - hospScreen.y;
            if (sqrtf(dx * dx + dy * dy) < 40)
            {
                hoverHospital = true;
            }
        }

        // ===== DRAW =====
        BeginDrawing();
        ClearBackground(Color{180, 210, 180, 255});

        // Statistics Dashboard (top)
        DrawRectangle(0, 0, screenW - 360, 40, Fade(BLACK, 0.8f));
        int totalHandled = 0;
        int totalPending = 0;
        for (auto &h : hospitals)
        {
            totalHandled += h.handled();
            totalPending += h.pendingCount();
        }
        string stats = "Total Emergencies: " + to_string(totalEmergencies) +
                            " | Handled: " + to_string(totalHandled) +
                            " | Pending: " + to_string(totalPending);
        DrawText(stats.c_str(), 20, 12, 16, WHITE);

        DrawRectangle((int)(startX - 300 + offsetX), (int)(startY - 300 + offsetY), (int)(mapWidth + 600), (int)(mapHeight + 600), Color{200, 230, 190, 255});

        // roads
        for (auto &r : roads)
        {
            Rectangle rect = r.rect;
            rect.x += offsetX;
            rect.y += offsetY;
            if (r.horizontal)
            {
                DrawRectangleRec(Rectangle{rect.x, rect.y - sidewalk, rect.width, sidewalk}, Color{200, 200, 200, 255});
                DrawRectangleRec(Rectangle{rect.x, rect.y + rect.height, rect.width, sidewalk}, Color{200, 200, 200, 255});
            }
            else
            {
                DrawRectangleRec(Rectangle{rect.x - sidewalk, rect.y, sidewalk, rect.height}, Color{200, 200, 200, 255});
                DrawRectangleRec(Rectangle{rect.x + rect.width, rect.y, sidewalk, rect.height}, Color{200, 200, 200, 255});
            }
            DrawRectangleRec(rect, Color{80, 80, 80, 255});
            float dash = 18.0f;
            if (r.horizontal)
                for (float x = rect.x + 6; x < rect.x + rect.width - 6; x += dash * 2)
                    DrawRectangle((int)x, (int)(rect.y + rect.height / 2 - 2), (int)dash, 4, Color{240, 230, 140, 200});
            else
                for (float y = rect.y + 6; y < rect.y + rect.height - 6; y += dash * 2)
                    DrawRectangle((int)(rect.x + rect.width / 2 - 2), (int)y, 4, (int)dash, Color{240, 230, 140, 200});
        }

        // houses
        for (auto &h : houses)
        {
            Rectangle hb = h.body;
            hb.x += offsetX;
            hb.y += offsetY;

            // Highlight if hovered or has emergency
            if (h.id == hoverHouse)
                DrawRectangleRec(Rectangle{hb.x - 4, hb.y - 4, hb.width + 8, hb.height + 8}, Fade(YELLOW, 0.3f));

            DrawTriangle(Vector2{hb.x + hb.width * 0.5f, hb.y - hb.height * 0.35f}, Vector2{hb.x - 2, hb.y + 3}, Vector2{hb.x + hb.width + 2, hb.y + 3}, Color{120, 80, 60, 255});
            DrawRectangleRec(hb, h.color);
            DrawRectangle((int)(hb.x + hb.width * 0.06f), (int)(hb.y + hb.height * 0.52f), (int)(hb.width * 0.16f), (int)(hb.height * 0.42f), Color{90, 50, 30, 255});
            DrawRectangle((int)(hb.x + hb.width * 0.43f), (int)(hb.y + hb.height * 0.26f), (int)(hb.width * 0.2f), (int)(hb.height * 0.18f), Color{200, 230, 255, 255});
            DrawRectangleLinesEx(Rectangle{hb.x + hb.width * 0.43f, hb.y + hb.height * 0.26f, hb.width * 0.2f, hb.height * 0.18f}, 1, BLACK);

            // Emergency indicator
            if (h.hasEmergency)
            {
                float pulse = (sinf((float)gameTime * 8.0f) + 1.0f) / 2.0f;
                DrawCircleV(Vector2{hb.x + hb.width / 2, hb.y - 8}, 6 + pulse * 3, Fade(RED, 0.8f));
                DrawText("!", (int)(hb.x + hb.width / 2 - 4), (int)(hb.y - 14), 16, WHITE);
            }

            // House number label
            string idStr = to_string(h.id);
            DrawText(idStr.c_str(), (int)(hb.x + hb.width / 2 - MeasureText(idStr.c_str(), 10) / 2), (int)(hb.y + hb.height + 2), 10, DARKGRAY);
        }

        // ambulances & paths
        for (size_t hi = 0; hi < hospitals.size(); ++hi)
        {
            auto &h = hospitals[hi];
            for (auto &amb : h.getAmbulances())
            {
                // Draw path
                if (!amb.path.empty())
                    for (size_t p = amb.currentPathIndex; p + 1 < amb.path.size(); ++p)
                    {
                        Vector2 a = amb.path[p], b = amb.path[p + 1];
                        DrawLineEx(Vector2{a.x + offsetX, a.y + offsetY}, Vector2{b.x + offsetX, b.y + offsetY}, 3, Fade(RED, 0.35f));
                    }

                // Draw ambulance
                Rectangle ab = amb.bounds();
                DrawRectangleRec(Rectangle{ab.x + offsetX, ab.y + offsetY, ab.width, ab.height}, amb.color);
                DrawRectangle((int)(ab.x + 4 + offsetX), (int)(ab.y + 2 + offsetY), (int)(ab.width - 8), (int)(ab.height - 4), WHITE);
                DrawRectangle((int)(ab.x + ab.width / 2 - 2 + offsetX), (int)(ab.y + ab.height / 2 - 6 + offsetY), 4, 12, RED);
                DrawRectangle((int)(ab.x + ab.width / 2 - 6 + offsetX), (int)(ab.y + ab.height / 2 - 2 + offsetY), 12, 4, RED);

                // Status label above ambulance
                string statusLabel = "A" + to_string(amb.id) + ": " + amb.getStatusString();
                int labelW = MeasureText(statusLabel.c_str(), 10);
                Vector2 labelPos = {amb.pos.x + offsetX - labelW / 2, amb.pos.y + offsetY - 20};
                DrawRectangle((int)labelPos.x - 2, (int)labelPos.y - 2, labelW + 4, 14, Fade(BLACK, 0.7f));
                DrawText(statusLabel.c_str(), (int)labelPos.x, (int)labelPos.y, 10, WHITE);

                // Timer for ON_SCENE
                if (amb.status == Ambulance::Status::ON_SCENE)
                {
                    string timer = to_string((int)amb.onSceneTimer + 1) + "s";
                    DrawText(timer.c_str(), (int)(amb.pos.x + offsetX - 8), (int)(amb.pos.y + offsetY + 12), 12, YELLOW);
                }

                // Parking spot indicator
                DrawCircleV(Vector2{amb.parkingPos.x + offsetX, amb.parkingPos.y + offsetY}, 4, Fade(DARKGRAY, 0.6f));
            }
        }

        // hospital
        // hospital
if (!hospitals.empty())
{
    Vector2 loc = hospitals[0].getLocation();
    
    // Draw parking zone background
    Rectangle parkingZone = {
        loc.x + offsetX - 90,
        loc.y + offsetY + 15,
        180,
        35
    };
    DrawRectangleRec(parkingZone, Fade(Color{60, 60, 80, 255}, 0.3f));
    DrawRectangleLinesEx(parkingZone, 2, Fade(WHITE, 0.5f));
    
    // Draw parking spots
    for (auto &amb : hospitals[0].getAmbulances())
    {
        DrawRectangle((int)(amb.parkingPos.x + offsetX - 8), 
                     (int)(amb.parkingPos.y + offsetY - 6), 
                     16, 12, Fade(DARKGRAY, 0.4f));
        DrawRectangleLinesEx(Rectangle{amb.parkingPos.x + offsetX - 8, 
                                      amb.parkingPos.y + offsetY - 6, 
                                      16, 12}, 1, WHITE);
    }
    
    // Highlight if hovering
    if (hoverHospital)
    {
        DrawCircleV(Vector2{loc.x + offsetX, loc.y + offsetY}, 36, Fade(YELLOW, 0.3f));
    }
            
            DrawCircleV(Vector2{loc.x + offsetX, loc.y + offsetY}, 12, BLUE);
            DrawCircleV(Vector2{loc.x + offsetX, loc.y + offsetY}, 8, WHITE);
            DrawText("+", (int)(loc.x + offsetX - 4), (int)(loc.y + offsetY - 6), 16, RED);
            string label = "HOSPITAL";
            DrawText(label.c_str(), (int)(loc.x + 16 + offsetX), (int)(loc.y - 8 + offsetY), 12, BLACK);
        }

        // === Hospital Hover Details Panel ===
        if (hoverHospital && !hospitals.empty())
        {
            Vector2 hospLoc = hospitals[0].getLocation();
            Vector2 panelPos = {hospLoc.x + offsetX + 50, hospLoc.y + offsetY - 100};
            
            // Calculate panel size based on content
            int numAmbs = hospitals[0].getAmbulances().size();
            float panelHeight = 60 + numAmbs * 50;
            float panelWidth = 320;
            
            // Adjust panel position if it goes off screen
            if (panelPos.x + panelWidth > screenW - 360)
                panelPos.x = hospLoc.x + offsetX - panelWidth - 50;
            if (panelPos.y < 0)
                panelPos.y = 10;
            
            Rectangle detailPanel = {panelPos.x, panelPos.y, panelWidth, panelHeight};
            
            // Draw panel
            DrawRectangleRec(detailPanel, Fade(Color{30, 30, 40, 255}, 0.95f));
            DrawRectangleLinesEx(detailPanel, 3, SKYBLUE);
            
            // Title
            DrawText("HOSPITAL STATUS", (int)detailPanel.x + 10, (int)detailPanel.y + 10, 16, SKYBLUE);
            DrawLine((int)detailPanel.x + 10, (int)detailPanel.y + 32, (int)(detailPanel.x + detailPanel.width - 10), (int)detailPanel.y + 32, SKYBLUE);
            
            // Ambulance details
            float yPos = detailPanel.y + 40;
            for (auto &amb : hospitals[0].getAmbulances())
            {
                Color statusColor;
                string statusText;
                
                if (amb.status == Ambulance::Status::IDLE)
                {
                    statusColor = GREEN;
                    statusText = "IDLE - Ready for dispatch";
                }
                else if (amb.status == Ambulance::Status::TO_SCENE)
                {
                    statusColor = ORANGE;
                    statusText = "EN ROUTE to House #" + to_string(amb.assignedHouseId);
                }
                else if (amb.status == Ambulance::Status::ON_SCENE)
                {
                    statusColor = RED;
                    statusText = "ON SCENE at House #" + to_string(amb.assignedHouseId);
                }
                else if (amb.status == Ambulance::Status::RETURNING)
                {
                    statusColor = YELLOW;
                    statusText = "RETURNING to hospital";
                }
                
                // Ambulance ID and status indicator
                DrawCircleV(Vector2{detailPanel.x + 16, yPos + 8}, 5, statusColor);
                DrawText(("Ambulance #" + to_string(amb.id)).c_str(), (int)detailPanel.x + 26, (int)yPos, 14, WHITE);
                
                // Status details
                DrawText(statusText.c_str(), (int)detailPanel.x + 26, (int)yPos + 16, 11, LIGHTGRAY);
                
                // Patient info if assigned
                if (!amb.assignedPatientName.empty() && amb.status != Ambulance::Status::IDLE)
                {
                    string patientInfo = "Patient: " + amb.assignedPatientName;
                    DrawText(patientInfo.c_str(), (int)detailPanel.x + 26, (int)yPos + 30, 10, Color{180, 220, 255, 255});
                }
                
                yPos += 50;
            }
        }

        // === UI PANELS ===

        // Form Panel
        DrawRectangleRec(formPanel, Fade(WHITE, 0.95f));
        DrawRectangleLinesEx(formPanel, 2, DARKGRAY);
        DrawText("EMERGENCY REPORT", (int)formPanel.x + 12, (int)formPanel.y + 6, 16, BLACK);
        tfName.draw("Patient Name");
        tfAge.draw("Age");
        Rectangle sevRect = {formPanel.x + 12, formPanel.y + 140, formPanel.width - 24, 28};
        Color sevColor = (severityIdx == 2) ? RED : (severityIdx == 1 ? ORANGE : GREEN);
        DrawRectangleRec(sevRect, Fade(sevColor, 0.2f));
        DrawRectangleLinesEx(sevRect, 1, sevColor);
        DrawText("Severity (click to change)", (int)sevRect.x, (int)sevRect.y - 18, 12, DARKGRAY);
        DrawText(severities[severityIdx].c_str(), (int)sevRect.x + 6, (int)sevRect.y - 2, 14, BLACK);
        tfDesc.draw("Description");
        tfHouse.draw("House Number");
        DrawRectangleRec(btnSubmit, Fade(Color{100, 200, 100, 255}, 0.9f));
        DrawRectangleLinesEx(btnSubmit, 2, DARKGREEN);
        DrawText("SUBMIT", (int)btnSubmit.x + 36, (int)btnSubmit.y + 10, 16, BLACK);
        DrawRectangleRec(btnClear, Fade(LIGHTGRAY, 0.9f));
        DrawRectangleLinesEx(btnClear, 2, DARKGRAY);
        DrawText("CLEAR", (int)btnClear.x + 44, (int)btnClear.y + 10, 16, BLACK);

        // Activity log at bottom of form
        float logY = formPanel.y + 370;
        DrawText("Recent Activity:", (int)formPanel.x + 12, (int)logY, 12, DARKGRAY);
        logY += 16;
        for (size_t i = 0; i < activityLog.size() && i < 5; ++i)
        {
            auto &log = activityLog[i];
            string timeStr = to_string((int)(gameTime - log.timestamp)) + "s ago";
            DrawText(log.message.c_str(), (int)formPanel.x + 12, (int)(logY + i * 16), 10, log.color);
            DrawText(timeStr.c_str(), (int)formPanel.x + 220, (int)(logY + i * 16), 9, GRAY);
        }

        // Queue Panel
        DrawRectangleRec(queuePanel, Fade(WHITE, 0.95f));
        DrawRectangleLinesEx(queuePanel, 2, DARKGRAY);
        DrawText("EMERGENCY QUEUE", (int)queuePanel.x + 12, (int)queuePanel.y + 6, 16, BLACK);

        float qY = queuePanel.y + 30;
        
        DrawText("Hospital:", (int)queuePanel.x + 12, (int)qY, 14, DARKBLUE);
        qY += 18;

        auto pending = hospitals[0].peekAllPending();
        if (pending.empty())
        {
            DrawText("  No pending emergencies", (int)queuePanel.x + 16, (int)qY, 11, GRAY);
            qY += 14;
        }
        else
        {
            for (size_t j = 0; j < pending.size() && j < 3; ++j)
            {
                auto &em = pending[j];
                Color prioColor = (em.priority == 1) ? RED : (em.priority == 2 ? ORANGE : GREEN);
                string queueItem = "  #" + to_string(em.id) + " " + em.patient.name +
                                        " (" + em.patient.severity + ")";
                DrawText(queueItem.c_str(), (int)queuePanel.x + 16, (int)qY, 11, prioColor);
                qY += 14;
            }
            if (pending.size() > 3)
            {
                DrawText(("  +" + to_string(pending.size() - 3) + " more...").c_str(),
                         (int)queuePanel.x + 16, (int)qY, 10, GRAY);
                qY += 14;
            }
        }
        qY += 10;

        // Ambulance Status List
        DrawText("AMBULANCE STATUS:", (int)queuePanel.x + 12, (int)qY, 12, DARKGRAY);
        qY += 16;
        for (auto &amb : hospitals[0].getAmbulances())
        {
            Color statusColor = (amb.status == Ambulance::Status::IDLE) ? GREEN : (amb.status == Ambulance::Status::ON_SCENE) ? RED : ORANGE;
            string ambStatus = "A" + to_string(amb.id) + ": " + amb.getStatusString();
            DrawCircleV(Vector2{queuePanel.x + 18, qY + 6}, 4, statusColor);
            DrawText(ambStatus.c_str(), (int)queuePanel.x + 26, (int)qY, 10, BLACK);
            qY += 14;
        }

        // Bottom info bar
        if (hoverHouse != -1)
        {
            string info = "House #" + to_string(hoverHouse) + " - Enter this number in the form";
            DrawRectangle(0, screenH - 30, screenW - 360, 30, Fade(BLACK, 0.8f));
            DrawText(info.c_str(), 12, screenH - 22, 14, YELLOW);
        }
        else if (hoverHospital)
        {
            string info = "HOSPITAL - View ambulance details in the popup panel";
            DrawRectangle(0, screenH - 30, screenW - 360, 30, Fade(BLACK, 0.8f));
            DrawText(info.c_str(), 12, screenH - 22, 14, SKYBLUE);
        }
        else
        {
            DrawRectangle(0, screenH - 30, screenW - 360, 30, Fade(BLACK, 0.8f));
            DrawText("Controls: Arrow Keys = Pan | Hover house/hospital for details | Use form to report emergency", 12, screenH - 22, 14, WHITE);
        }

        EndDrawing();
    }

    CloseWindow();
    return 0;
} 