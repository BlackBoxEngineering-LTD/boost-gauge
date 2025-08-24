#include <iostream>
#include <cmath>
#include <chrono>
#include <thread>

using namespace std;

float psiToAngle(float psi) {
    if (psi <= 0.0f) {
        float frac = (psi + 15.0f) / 15.0f;
        return 270.0f + frac * -90.0f;
    } else {
        float frac = psi / 15.0f;
        return 180.0f + frac * -180.0f;
    }
}

float barToPsi(float bar) { return bar * 14.5038f; }

int main() {
    float simTime = 0.0f;
    cout << "Boost Gauge Simulator\n";
    cout << "Press Ctrl+C to exit\n\n";
    while (true) {
        simTime += 0.02f;
        float simPsi = 15.0f * sin(simTime);
        float bar = simPsi / 14.5038f;
        float angle = psiToAngle(simPsi);
        cout << "\rPSI: " << simPsi << " | BAR: " << bar 
             << " | Angle: " << angle << " | " 
             << (simPsi < 0 ? "VACUUM" : "BOOST") << "    ";
        cout.flush();
        this_thread::sleep_for(chrono::milliseconds(30));
    }
    
    return 0;
}