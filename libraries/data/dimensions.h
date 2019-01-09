/***************************************************************************
 *
 * Authors:    David Strelak (davidstrelak@gmail.com)
 *
 * Unidad de  Bioinformatica of Centro Nacional de Biotecnologia , CSIC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307  USA
 *
 *  All comments concerning this program package may be sent to the
 *  e-mail address 'xmipp@cnb.csic.es'
 ***************************************************************************/

#ifndef LIBRARIES_DATA_DIMENSIONS_H_
#define LIBRARIES_DATA_DIMENSIONS_H_

#include <cstddef>

class Dimensions {
public:
    explicit Dimensions(size_t x, size_t y = 1, size_t z = 1, size_t n = 1) :
            x(x), y(y), z(z), n(n) {
    }
    ;

    constexpr size_t size() const {
        return x * y * z * n;
    }

    const size_t x;
    const size_t y;
    const size_t z;
    const size_t n;

    friend std::ostream& operator<<(std::ostream &os, const Dimensions &d) {
        os << d.x << " * " << d.y << " * " << d.z << " * " << d.n;
        return os;
    }
};



#endif /* LIBRARIES_DATA_DIMENSIONS_H_ */
