#include "config/PlantCatalog.h"

const QVector<PlantDefinition>& PlantCatalog::all()
{
    static const QVector<PlantDefinition> definitions = {
        {0, QStringLiteral("橡树"), QStringLiteral("OakTree"), QStringLiteral("oak"), QStringLiteral(":/images/oak_final.png"), 200, QColor("#69B966")},
        {1, QStringLiteral("松树"), QStringLiteral("PineTree"), QStringLiteral("pine"), QStringLiteral(":/images/pine_final.png"), 500, QColor("#4E9C63")},
        {2, QStringLiteral("玫瑰"), QStringLiteral("Rose"), QStringLiteral("rose"), QStringLiteral(":/images/rose_final.png"), 500, QColor("#D18457")},
        {3, QStringLiteral("银杏树"), QStringLiteral("GinkgoTree"), QStringLiteral("ginkgo"), QStringLiteral(":/images/ginkgo_final.png"), 1000, QColor("#E4BE45")},
        {4, QStringLiteral("向日葵"), QStringLiteral("Sunflower"), QStringLiteral("sunflower"), QStringLiteral(":/images/sunflower_final.png"), 1000, QColor("#F1C85B")},
        {5, QStringLiteral("仙人掌"), QStringLiteral("Cactus"), QStringLiteral("cactus"), QStringLiteral(":/images/cactus_final.png"), 1500, QColor("#C87145")},
    };
    return definitions;
}

const PlantDefinition& PlantCatalog::byType(uint32_t type)
{
    const auto& definitions = all();
    for (const PlantDefinition& definition : definitions) {
        if (definition.type == type) return definition;
    }
    return definitions.first();
}

bool PlantCatalog::isKnown(uint32_t type)
{
    for (const PlantDefinition& definition : all()) {
        if (definition.type == type) return true;
    }
    return false;
}

QString PlantCatalog::stageImagePath(uint32_t type, uint32_t stage, bool withered)
{
    if (stage == 0 && !withered) return QStringLiteral(":/images/seedplanted.png");
    const QString& key = byType(type).assetKey;
    if (withered) return QStringLiteral(":/images/%1_withered.png").arg(key);
    static const QStringList suffixes = {QStringLiteral("initial"), QStringLiteral("middle"), QStringLiteral("final")};
    const int index = qBound(1, static_cast<int>(stage), 3) - 1;
    return QStringLiteral(":/images/%1_%2.png").arg(key, suffixes[index]);
}
