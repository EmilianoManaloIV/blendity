// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace bl {

struct Lesson {
  const char *title;
  const char *subtitle;
  const char *body;  // lightweight markup, see lessons.cpp
};

int lesson_count();
const Lesson &lesson(int i);

}  // namespace bl
