#pragma once

// 为不提供拷贝构造和赋值运算符的类添加了nocopyable基类
class nocopyable
{
protected:
    nocopyable() {}
    ~nocopyable() {}

private:
    nocopyable(const nocopyable&);
    const nocopyable &operator=(const nocopyable&);
};