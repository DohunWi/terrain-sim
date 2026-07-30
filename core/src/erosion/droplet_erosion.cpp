#include <algorithm>
#include <cmath>
#include <random>
#include "droplet_erosion.h"

struct Droplet{
    float posX;
    float posY;
    float dirX;
    float dirY;
    float speed;
    float water;
    float sediment;
};
static void erode(Heightmap& hm, float x, float y, float erodeAmount, float tx, float ty){
    int x0 = floor(x);
    int x1 = x0 +1;
    int y0 = floor(y);
    int y1 = y0 + 1;

    hm.at(x1,y1)-= erodeAmount*tx*ty;
    hm.at(x0,y1) -= erodeAmount*(1-tx)*ty;
    hm.at(x1,y0) -= erodeAmount*tx*(1-ty);
    hm.at(x0,y0) -= erodeAmount*(1-tx)*(1-ty);
}
static void deposit(Heightmap& hm, float x, float y, float depositAmount, float tx, float ty){
    int x0 = floor(x);
    int x1 = x0 +1;
    int y0 = floor(y);
    int y1 = y0 + 1;

    hm.at(x1,y1) += depositAmount*tx*ty;
    hm.at(x0,y1) += depositAmount*(1-tx)*ty;
    hm.at(x1,y0) += depositAmount*tx*(1-ty);
    hm.at(x0,y0) += depositAmount*(1-tx)*(1-ty);
}

static int dropletStep(Heightmap& hm, Droplet& droplet, const ErosionParams& params){
    HeightSample hs = hm.sample(droplet.posX, droplet.posY);
    float previousX = droplet.posX;
    float previousY = droplet.posY;

    droplet.dirX = droplet.dirX * params.inertia - (1 - params.inertia) * hs.gradX;
    droplet.dirY = droplet.dirY * params.inertia - (1 - params.inertia) * hs.gradY;
    float magnitude = sqrt(droplet.dirX * droplet.dirX + droplet.dirY * droplet.dirY);

    // normalization
    if(magnitude == 0){
        droplet.dirX = 0;
        droplet.dirY = 0;
    }
    else{
        droplet.dirX /= magnitude;
        droplet.dirY /= magnitude;
    }

    droplet.posX += droplet.dirX;
    droplet.posY += droplet.dirY;

    if(droplet.posX < 0 || floor(droplet.posX) >= (hm.width()-1)){
        return 0;
    }
    if(droplet.posY < 0 || floor(droplet.posY) >= (hm.height()-1)){
        return 0;
    }

    HeightSample nextHs = hm.sample(droplet.posX, droplet.posY);
    float deltaHeight = nextHs.height - hs.height;
    float capacity = std::max(-deltaHeight, params.minSlope) * droplet.speed * droplet.water * params.capacityFactor;


    // Erode / deposit
    if(droplet.sediment > capacity || deltaHeight > 0){
        // deposit
        float depositAmount = (deltaHeight > 0) ? std::min(deltaHeight, droplet.sediment):(droplet.sediment - capacity) * params.depositFactor;
        droplet.sediment -= depositAmount;
        deposit(hm, previousX, previousY, depositAmount, hs.tx, hs.ty);
    }
    else { // erode
        float erodeAmount = std::min((capacity - droplet.sediment)*params.erosionFactor, -deltaHeight);
        droplet.sediment += erodeAmount;
        erode(hm, previousX, previousY, erodeAmount, hs.tx, hs.ty);
    }

    //speed
    droplet.speed = sqrt(std::max(0.0f, droplet.speed*droplet.speed - deltaHeight*params.gravity));

    //water
    droplet.water *= (1 - params.evaporateRate);

    return 1;
}
double dropletErode(Heightmap& hm, const ErosionParams& params, int numDroplets, unsigned seed){
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> distX(1.0f, float(hm.width()-2));
    std::uniform_real_distribution<float> distY(1.0f, float(hm.height()-2));

    double totalCarriedSediment = 0.0;

    for(int i=0; i < numDroplets; i++){
        Droplet droplet;
        droplet.posX = distX(rng);
        droplet.posY = distY(rng);
        droplet.dirX = 0.0f;
        droplet.dirY = 0.0f;
        droplet.speed = 1.0f;
        droplet.water = 1.0f;
        droplet.sediment = 0.0f;

        int dropletState = 1;
        int steps = 0;
        while (steps < params.maxLifeTime && droplet.water > params.waterThreshold && dropletState==1){
            dropletState = dropletStep(hm, droplet, params);
            steps++;
        }
        totalCarriedSediment += droplet.sediment;
    }

    return totalCarriedSediment;
}
