#pragma once

#include <vector>

// (x, y) 주변 4개 격자점을 bilinear 보간한 높이/기울기 쿼리 결과.
// tx, ty는 (x, y)가 자기가 속한 셀 안에서 가진 소수부(0~1) — 셀 왼쪽/아래
// 모서리로부터의 상대 위치. erode/deposit이 그 4개 격자점에 가중치로
// 나눠 쓰는 게 바로 이 tx, ty.
struct HeightSample {
    float height;
    float tx;
    float ty;
    float gradX;
    float gradY;
};

// 2D 높이맵. 메모리는 1차원 vector에 y * width_ + x 로 평탄화해서 저장한다.
// (C의 malloc(width*height) 한 덩어리, numpy 2D 배열과 같은 레이아웃.
//  vector<vector<float>>로 안 짜는 이유: 그건 행마다 따로 힙 할당이라 캐시 지역성이 나쁨.)
class Heightmap {
public:
    Heightmap(int width, int height);

    // 참조를 리턴하는 버전: h.at(x, y) = 1.5f; 처럼 값을 쓸 수 있음.
    float& at(int x, int y);
    // const 버전: 읽기 전용 Heightmap 객체에서도 호출 가능.
    float at(int x, int y) const;

    // 연속 좌표 (x, y)에서 bilinear 보간한 높이와 기울기를 구한다.
    // droplet erosion과 (앞으로의) physics 양쪽이 지형을 "쿼리"하는
    // 공통 경로라서 at()과 같은 급의 멤버 함수로 둠.
    HeightSample sample(float x, float y) const;

    int width() const { return width_; }
    int height() const { return height_; }

private:
    int width_;
    int height_;
    std::vector<float> data_;
    // 소멸자를 따로 안 만들어도 됨: 이 객체가 스코프를 벗어나면
    // data_ (vector)의 소멸자가 자동으로 호출돼서 메모리를 해제함.
    // 이게 RAII — "자원 해제를 자원을 소유한 객체의 생명주기에 묶는다".
};
