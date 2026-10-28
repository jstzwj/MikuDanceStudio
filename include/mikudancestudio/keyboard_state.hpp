#pragma once

namespace mikudancestudio {

// Original shared slots for letter-key edges and menu re-entry latches.
// Keep their numeric mapping: both keyboard poll tables address this array.
enum class LetterKey : int {
    X = 0,
    Z = 1,
    C = 2,
    V = 3,
    D = 4,
    A = 5,
    B = 6,
    G = 7,
    S = 8,
    I = 9,
    H = 10,
    K = 11,
    P = 12,
    U = 13,
    J = 14,
    F = 15,
    R = 16,
    L = 17,
};

} // namespace mikudancestudio
