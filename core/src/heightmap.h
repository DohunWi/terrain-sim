#pragma once

#include <vector>

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
