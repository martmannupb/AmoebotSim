#include <cmath>

#include <QImage>
#include <QMutexLocker>
#include <QOpenGLFunctions_2_0>
#include <QQuickWindow>
#include <QRgb>

#include "ui/visitem.h"

// visualisation preferences
static constexpr float targetFramesPerSecond = 60.0f;

// values derived from the preferences above
static constexpr float targetFrameDuration = 1000.0f / targetFramesPerSecond;

// height of a triangle in our equilateral triangular grid if the side length is 1
static const float triangleHeight = sqrtf(3.0f / 4.0f);

VisItem::VisItem(QQuickItem* parent) :
    GLItem(parent),
    translating(false)
{
    setAcceptedMouseButtons(Qt::LeftButton);

    renderTimer.start(targetFrameDuration);
}

void VisItem::systemChanged(std::shared_ptr<System> _system)
{
    system = _system;
}

void VisItem::focusOnCenterOfMass()
{
    QMutexLocker locker(&system->mutex);
    if(system == nullptr || system->size() == 0) {
        return;
    }

    QPointF sum;
    int numNodes = 0;

    for(const Particle& p : *system) {
        sum = sum + nodeToWorldCoord(p.head);
        numNodes++;
        if(p.globalTailDir != -1) {
            sum = sum + nodeToWorldCoord(p.tail());
            numNodes++;
        }
    }

    view.setFocusPos(sum / numNodes);
}

void VisItem::initialize()
{
    gridTex = std::unique_ptr<QOpenGLTexture>(new QOpenGLTexture(QImage(":/textures/grid.png").mirrored()));
    gridTex->setMinMagFilters(QOpenGLTexture::LinearMipMapLinear, QOpenGLTexture::Linear);
    gridTex->setWrapMode(QOpenGLTexture::Repeat);
    gridTex->bind();
    gridTex->generateMipMaps();

    particleTex = std::unique_ptr<QOpenGLTexture>(new QOpenGLTexture(QImage(":textures/particle.png").mirrored()));
    particleTex->setMinMagFilters(QOpenGLTexture::LinearMipMapLinear, QOpenGLTexture::Linear);
    particleTex->bind();
    particleTex->generateMipMaps();

    Q_ASSERT(window() != nullptr);
    connect(&renderTimer, &QTimer::timeout, window(), &QQuickWindow::update);
}

void VisItem::paint()
{
    glfn->glUseProgram(0);

    glfn->glViewport(0, 0, width(), height());

    glfn->glDisable(GL_DEPTH_TEST);
    glfn->glDisable(GL_CULL_FACE);

    glfn->glEnable(GL_BLEND);
    glfn->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glfn->glEnable(GL_TEXTURE_2D);

    setupCamera();

    drawGrid();

    if(system != nullptr) {
        QMutexLocker locker(&system->mutex);
        drawParticles();
    }
}

void VisItem::deinitialize()
{
    renderTimer.disconnect();

    particleTex = nullptr;
    gridTex = nullptr;
}

void VisItem::sizeChanged(int width, int height)
{
    view.setViewportSize(width, height);
}

void VisItem::setupCamera()
{
    glfn->glMatrixMode(GL_MODELVIEW);
    glfn->glLoadIdentity();
    glfn->glMatrixMode(GL_PROJECTION);
    glfn->glLoadIdentity();
    glfn->glOrtho(view.left(), view.right(), view.bottom(), view.top(), 1, -1);
}

void VisItem::drawGrid()
{
    // gridTex has the height of two triangles
    static const float gridTexHeight = 2.0f * triangleHeight;

    // Coordinate sytem voodoo:
    // Calculates the texture coordinates of the corners of the shown part of the grid.
    const float left = fmodf(view.left(), 1.0f);
    const float right = left + view.right() - view.left();
    const float bottom = fmodf(view.bottom(), gridTexHeight) / gridTexHeight;
    const float top = bottom + (view.top() - view.bottom()) / gridTexHeight;

    // Draw screen-filling quad with gridTex according to above texture coordinates.
    gridTex->bind();
    glfn->glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glfn->glBegin(GL_QUADS);
    glfn->glTexCoord2f(left, bottom);
    glfn->glVertex2f(view.left(), view.bottom());
    glfn->glTexCoord2f(right, bottom);
    glfn->glVertex2f(view.right(), view.bottom());
    glfn->glTexCoord2f(right, top);
    glfn->glVertex2f(view.right(), view.top());
    glfn->glTexCoord2f(left, top);
    glfn->glVertex2f(view.left(), view.top());
    glfn->glEnd();
}

void VisItem::drawParticles()
{
    particleTex->bind();
    glfn->glBegin(GL_QUADS);
    for(const Particle& p : *system) {
        if(view.includes(nodeToWorldCoord(p.head))) {
            drawMarks(p);
        }
    }
    for(const Particle& p : *system) {
        if(view.includes(nodeToWorldCoord(p.head))) {
            drawParticle(p);
        }
    }
    for(const Particle& p : *system) {
        if(view.includes(nodeToWorldCoord(p.head))) {
            drawBorders(p);
        }
    }
    for(const Particle& p : *system) {
        if(view.includes(nodeToWorldCoord(p.head))) {
            drawBorderPoints(p);
        }
    }
    glfn->glEnd();
}

void VisItem::drawMarks(const Particle& p)
{
    // draw mark around head
    if(p.headMarkColor() != -1) {
        auto pos = nodeToWorldCoord(p.head);
        auto color = p.headMarkColor();
        glfn->glColor4i(qRed(color) << 23, qGreen(color) << 23, qBlue(color) << 23, 180 << 23);
        drawFromParticleTex(p.headMarkGlobalDir() + 8, pos);
    }

    // draw mark around tail
    if(p.globalTailDir != -1 && p.tailMarkColor() > -1) {
        auto pos = nodeToWorldCoord(p.tail());
        auto color = p.tailMarkColor();
        glfn->glColor4i(qRed(color) << 23, qGreen(color) << 23, qBlue(color) << 23, 180 << 23);
        drawFromParticleTex(p.tailMarkGlobalDir() + 8, pos);
    }
}

void VisItem::drawParticle(const Particle& p)
{
    auto pos = nodeToWorldCoord(p.head);
    glfn->glColor4f(0.0f, 0.0f, 0.0f, 1.0f);
    drawFromParticleTex(p.globalTailDir + 1, pos);
}

void VisItem::drawBorders(const Particle& p)
{
    auto pos = nodeToWorldCoord(p.head);
    for(unsigned int i = 0; i < p.borderColors().size(); ++i) {
        if(p.borderColors().at(i) != -1) {
            auto color = p.borderColors().at(i);
            glfn->glColor4i(qRed(color) << 23, qGreen(color) << 23, qBlue(color) << 23, 180 << 23);
            drawFromParticleTex(i + 21, pos);
        }
    }
}

void VisItem::drawBorderPoints(const Particle& p)
{
    auto pos = nodeToWorldCoord(p.head);
    for(unsigned int i = 0; i < p.borderPointColors().size(); ++i) {
        if(p.borderPointColors().at(i) != -1) {
            auto color = p.borderPointColors().at(i);
            glfn->glColor4i(qRed(color) << 23, qGreen(color) << 23, qBlue(color) << 23, 255 << 23);
            drawFromParticleTex(i + 15, pos);
        }
    }
}

void VisItem::drawFromParticleTex(int index, const QPointF& pos)
{
    // these values are a consequence of how the particle texture was created
    static constexpr int texSize = 8;
    static constexpr float invTexSize = 1.0f / texSize;
    static constexpr float halfQuadSideLength = 256.0f / 220.0f;

    const float column = index % texSize;
    const float row = index / texSize;
    const QPointF texOffset(invTexSize * column, invTexSize * row);

    glfn->glTexCoord2f(texOffset.x(), texOffset.y());
    glfn->glVertex2f(pos.x() - halfQuadSideLength, pos.y() - halfQuadSideLength);
    glfn->glTexCoord2f(texOffset.x() + invTexSize, texOffset.y());
    glfn->glVertex2f(pos.x() + halfQuadSideLength, pos.y() - halfQuadSideLength);
    glfn->glTexCoord2f(texOffset.x() + invTexSize, texOffset.y() + invTexSize);
    glfn->glVertex2f(pos.x() + halfQuadSideLength, pos.y() + halfQuadSideLength);
    glfn->glTexCoord2f(texOffset.x(), texOffset.y() + invTexSize);
    glfn->glVertex2f(pos.x() - halfQuadSideLength, pos.y() + halfQuadSideLength);
}

QPointF VisItem::nodeToWorldCoord(const Node& node)
{
    return QPointF(node.x + 0.5f * node.y, node.y * triangleHeight);
}

Node VisItem::worldCoordToNode(const QPointF& worldCord)
{
    const int y = std::round(worldCord.y() / triangleHeight);
    const int x = std::round(worldCord.x() - 0.5 * y);
    return Node(x, y);
}

QPointF VisItem::windowCoordToWorldCoord(const QPointF& windowCoord)
{
    const float x = view.left() + (view.right() - view.left()) * windowCoord.x() / width();
    const float y = view.top() + (view.bottom() - view.top() ) * windowCoord.y() / height();
    return QPointF(x, y);
}

void VisItem::mousePressEvent(QMouseEvent* e)
{
    if(e->buttons() & Qt::LeftButton) {
        if(e->modifiers() & Qt::ControlModifier) {
            translating = false;
            auto node = worldCoordToNode(windowCoordToWorldCoord(e->localPos()));
            emit roundForParticleAt(node);
        } else {
            translating = true;
            lastMousePos = e->localPos();
        }
        e->accept();
    }
}

void VisItem::mouseMoveEvent(QMouseEvent* e)
{
    if(e->buttons() & Qt::LeftButton) {
        if(translating){
            auto mouseOffset = lastMousePos - e->localPos();
            view.modifyFocusPos(QPointF(mouseOffset.x(), -mouseOffset.y()));
            lastMousePos = e->localPos();
            e->accept();
        }
    }
}

void VisItem::wheelEvent(QWheelEvent* e)
{
    QPointF mousePos(QPointF(e->posF().x(), height() - e->posF().y()));
    auto mouseAngleDelta = e->angleDelta().y();
    view.modifyZoom(mousePos, mouseAngleDelta);
    e->accept();
}
