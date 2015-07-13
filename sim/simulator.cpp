#include <QScriptValue>
#include <QTimer>

#include "script/scriptinterface.h"
#include "sim/simulator.h"

Simulator::Simulator()
{
    engine.setGlobalObject(engine.newQObject(new ScriptInterface(*this), QScriptEngine::ScriptOwnership));
}

Simulator::~Simulator()
{
}

void Simulator::setSystem(std::shared_ptr<System> _system)
{
    if(roundTimer != nullptr) {
        roundTimer->stop();
        emit stopped();
    }
    system = _system;
}

void Simulator::init()
{
    if(roundTimer == nullptr) {
        roundTimer = std::make_shared<QTimer>(this);
        roundTimer->setInterval(100);
        connect(roundTimer.get(), &QTimer::timeout, this, &Simulator::round);

        updateTimer = std::make_shared<QTimer>(this);
        updateTimer->setInterval(33);
        connect(updateTimer.get(), &QTimer::timeout, [&](){emit updateSystem(std::make_shared<System>(*system));});
        updateTimer->start();
    }
}

//Is called when thread in which simulator is living is about to finish -> Clean up the simulator.
void Simulator::finished(){
    roundTimer->stop();
    updateTimer->stop();
}

void Simulator::round()
{
    system->round();
    auto systemState = system->getSystemState();
    if(systemState == System::SystemState::Deadlocked) {
        log("Deadlock detected. Simulation aborted.", true);
        roundTimer->stop();
        emit stopped();
    } else if(systemState == System::SystemState::Disconnected) {
        log("System disconnected. Simulation aborted.", true);
        roundTimer->stop();
        emit stopped();
    } else if(systemState == System::SystemState::Terminated) {
        log("Algorithm terminated. Simulation finished.", false);
        roundTimer->stop();
        emit stopped();
    }

#ifdef QT_DEBUG
    // increases the chance that when the debugger stops the visualization shows the actual configuration of the system
    emit updateSystem(std::make_shared<System>(*system));
#endif
}

void Simulator::start()
{
    roundTimer->start();
    emit started();
}

void Simulator::stop()
{
    roundTimer->stop();
    emit stopped();
}

void Simulator::roundForParticleAt(const int x, const int y)
{
    if(!roundTimer->isActive()) {
        const Node node(x, y);
        system->roundForParticle(node);
        auto systemState = system->getSystemState();
        if(systemState == System::SystemState::Deadlocked) {
            log("Deadlock detected.", true);
        } else if(systemState == System::SystemState::Disconnected) {
            log("System disconnected.", true);
        } else if(systemState == System::SystemState::Terminated) {
            log("Algorithm terminated.", false);
        }

    #ifdef QT_DEBUG
        // increases the chance that when the debugger stops the visualization shows the actual configuration of the system
        emit updateSystem(std::make_shared<System>(*system));
    #endif
    }
}

void Simulator::executeCommand(const QString cmd)
{
    QScriptValue result = engine.evaluate(cmd);
    if(!result.isUndefined()) {
        emit log(result.toString(), result.isError());
    }
}

void Simulator::runScript(const QString script)
{
    engine.evaluate(script);
}

void Simulator::abortScript()
{
    engine.abortEvaluation();
}

bool Simulator::getSystemValid()
{
    return system->getSystemState() == System::SystemState::Valid;
}

bool Simulator::getSystemDisconnected()
{
    return system->getSystemState() == System::SystemState::Disconnected;
}

bool Simulator::getSystemTerminated()
{
    return system->getSystemState() == System::SystemState::Terminated;
}

bool Simulator::getSystemDeadlocked()
{
    return system->getSystemState() == System::SystemState::Deadlocked;
}

int Simulator::getNumParticles() const
{
    return system->size();
}

int Simulator::getNumMovements() const
{
    return system->getNumMovements();
}

void Simulator::setRoundDuration(int ms)
{
    roundTimer->setInterval(ms);
    emit roundDurationChanged(ms);
}

void Simulator::saveScreenshotSlot(const QString filePath)
{
    // first make sure the visualization has the most recent system
    emit updateSystem(std::make_shared<System>(*system));
    emit saveScreenshotSignal(filePath);
}
