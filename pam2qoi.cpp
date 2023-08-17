/*
 * Copyright (c) 2023 Flössie <floessie.mail@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{

	class Image final
	{
	public:
		struct Pixel {
			using Value = std::uint8_t;

			Value r = 0;
			Value g = 0;
			Value b = 0;
			Value a = 255;

			bool operator ==(const Pixel& other) const
			{
				return
					r == other.r
					&& g == other.g
					&& b == other.b
					&& a == other.a;
			}
		};

		Image() :
			width_(0),
			height_(0)
		{
		}

		Image(Image&& other) noexcept :
			width_(other.width_),
			height_(other.height_),
			pixels_(std::move(other.pixels_))
		{
		}

		Image& operator =(Image&& other)
		{
			if (this != &other) {
				width_ = other.width_;
				height_ = other.height_;

				pixels_ = std::move(other.pixels_);
			}

			return *this;
		}

		explicit operator bool() const
		{
			return width_ && height_;
		}

		void clearAndInitialize(std::size_t width, std::size_t height)
		{
			width_ = width;
			height_ = height;

			pixels_.assign(width * height, {});
			pixels_.shrink_to_fit();
		}

		std::size_t getWidth() const
		{
			return width_;
		}

		std::size_t getHeight() const
		{
			return height_;
		}

		Pixel getPixel(std::size_t x, std::size_t y) const
		{
			if (x < width_ && y < height_) {
				return pixels_[width_ * y + x];
			}

			return {};
		}

		void setPixel(std::size_t x, std::size_t y, const Pixel& value)
		{
			if (x < width_ && y < height_) {
				pixels_[width_ * y + x] = value;
			}
		}

	private:
		std::size_t width_;
		std::size_t height_;

		std::vector<Pixel> pixels_;
	};

	Image readPam(std::istream& stream)
	{
		Image res;

		char c;

		if (
			!stream.get(c)
			|| c != 'P'
			|| !stream.get(c)
			|| c != '7'
			|| !stream.get(c)
			|| c != '\n'
		) {
			throw std::runtime_error("Image is not a portable arbitrary map.");
		}

		const auto skip_to_eol =
			[&stream]()
			{
				char c;

				while (stream.get(c)) {
					if (c == '\n') {
						break;
					}
				}
			};

		const auto skip_ws =
			[&stream]()
			{
				char c;

				while (stream.get(c)) {
					if (
						c != '\t'
						&& c != '\r'
						&& c != ' '
					) {
						stream.unget();
						break;
					}
				}
			};

		std::size_t width;
		std::size_t height;
		std::size_t depth;
		std::size_t max_value;
		std::string tuple_type;
		bool endhdr = false;

		while (stream.get(c)) {
			if (c == '#') {
				skip_to_eol();
				continue;
			}

			stream.unget();

			skip_ws();

			std::string token;

			if (!(stream >> token)) {
				throw std::runtime_error("Malformed PAM image header.");
			}

			const auto assign =
				[&stream, &skip_to_eol, &skip_ws](auto& out)
				{
					skip_ws();

					if (!(stream >> out)) {
						throw std::runtime_error("Malformed PAM image header.");
					}

					skip_to_eol();
				};

			if (token == "WIDTH") {
				assign(width);
			}
			else if (token == "HEIGHT") {
				assign(height);
			}
			else if (token == "DEPTH") {
				assign(depth);
			}
			else if (token == "MAXVAL") {
				assign(max_value);
			}
			else if (token == "TUPLTYPE") {
				assign(tuple_type);
			}
			else if (token == "ENDHDR") {
				endhdr = true;
				skip_to_eol();

				break;
			}
			else {
				skip_to_eol();
			}
		}

		if (!endhdr) {
			throw std::runtime_error("Malformed PAM image header.");
		}

		if (
			max_value != 255
			|| (
				(
					depth != 3
					|| tuple_type != "RGB"
				)
				&& (
					depth != 4
					|| tuple_type != "RGB_ALPHA"
				)
			)
		) {
			throw std::runtime_error("Unsupported PAM format.");
		}

		res.clearAndInitialize(width, height);

		std::vector<char> line_buffer(depth * width);

		for (std::size_t y = 0; y < height; ++y) {
			stream.read(line_buffer.data(), depth * width);

			if (!stream) {
				throw std::runtime_error("Corrupt PAM image body.");
			}

			std::vector<char>::size_type index = 0;

			for (std::size_t x = 0; x < width; ++x) {
				Image::Pixel pixel;

				for (unsigned int p = 0; p < depth; ++p, ++index) {
					switch (p) {
						case 0: {
							pixel.r = line_buffer[index];
							break;
						}

						case 1: {
							pixel.g = line_buffer[index];
							break;
						}

						case 2: {
							pixel.b = line_buffer[index];
							break;
						}

						case 3: {
							pixel.a = line_buffer[index];
							break;
						}
					}
				}

				res.setPixel(x, y, pixel);
			}
		}

		return res;
	}

	std::string encodeQoi(
		const Image& image,
		std::size_t start_y,
		std::size_t end_y
	)
	{
		enum class Tag : std::uint8_t {
			INDEX = 0x00,
			DIFF = 0x40,
			LUMA = 0x80,
			RUN = 0xC0,
			LONG_RUN = 0xFD,
			RGB = 0xFE,
			RGBA = 0xFF
		};

		std::string res;
		res.reserve((end_y - start_y) * image.getWidth() * 4 * 2 / 3);

		if (start_y == 0) {
			// Header
			const auto encode_be =
				[&res](std::uint32_t value)
				{
					res.push_back(value >> 24);
					res.push_back(value >> 16);
					res.push_back(value >> 8);
					res.push_back(value);
				};

			res += "qoif";
			encode_be(image.getWidth());
			encode_be(image.getHeight());
			res.push_back(4);
			res.push_back(0);
		}

		// Body
		std::array<std::optional<Image::Pixel>, 64> index;

		if (start_y == 0) {
			index.fill(Image::Pixel{0, 0, 0, 0});
		}

		Image::Pixel previous_pixel;

		std::uint8_t run = 0;

		for (std::size_t y = start_y; y < end_y && y < image.getHeight(); ++y) {
			for (std::size_t x = 0; x < image.getWidth(); ++x) {
				const Image::Pixel pixel = image.getPixel(x, y);

				if (pixel == previous_pixel) {
					++run;

					if (run == 62) {
						res.push_back(static_cast<std::uint8_t>(Tag::LONG_RUN));
						run = 0;
					}

					continue;
				}

				if (run) {
					res.push_back(static_cast<std::uint8_t>(Tag::RUN) | (run - 1));
					run = 0;
				}

				const std::uint8_t hash = (pixel.r * 3 + pixel.g * 5 + pixel.b * 7 + pixel.a * 11) % 64;

				if (index[hash] && index[hash] == pixel) {
					res.push_back(static_cast<std::uint8_t>(Tag::INDEX) | hash);
					previous_pixel = pixel;

					continue;
				}

				index[hash] = pixel;

				if (pixel.a != previous_pixel.a) {
					res.push_back(static_cast<std::uint8_t>(Tag::RGBA));
					res.push_back(pixel.r);
					res.push_back(pixel.g);
					res.push_back(pixel.b);
					res.push_back(pixel.a);
					previous_pixel = pixel;

					continue;
				}

				const auto is_within =
					[](std::int8_t value, std::int8_t low, std::int8_t high) -> bool
					{
						return value >= low && value <= high;
					};

				const std::int8_t vr = pixel.r - previous_pixel.r;
				const std::int8_t vg = pixel.g - previous_pixel.g;
				const std::int8_t vb = pixel.b - previous_pixel.b;

				previous_pixel = pixel;

				if (
					is_within(vr, -2, 1)
					&& is_within(vg, -2, 1)
					&& is_within(vb, -2, 1)
				) {
					res.push_back(static_cast<std::uint8_t>(Tag::DIFF) | (vr + 2) << 4 | (vg + 2) << 2 | (vb + 2));

					continue;
				}

				const std::int8_t vg_r = vr - vg;
				const std::int8_t vg_b = vb - vg;

				if (
					is_within(vg_r, -8, 7)
					&& is_within(vg, -32, 31)
					&& is_within(vg_b, -8, 7)
				) {
					res.push_back(static_cast<std::uint8_t>(Tag::LUMA) | (vg + 32));
					res.push_back((vg_r + 8) << 4 | (vg_b + 8));

					continue;
				}

				res.push_back(static_cast<std::uint8_t>(Tag::RGB));
				res.push_back(pixel.r);
				res.push_back(pixel.g);
				res.push_back(pixel.b);
			}
		}

		if (run) {
			res.push_back(static_cast<std::uint8_t>(Tag::RUN) | (run - 1));
		}

		if (end_y + 1 >= image.getHeight()) {
			// End marker
			for (unsigned int i = 0; i < 7; ++i) {
				res.push_back(0);
			}

			res.push_back(1);
		}

		return res;
	}

}

int main(int argc, char** argv)
try
{
	const unsigned int threads =
		argc > 1
			? std::stoul(argv[1])
			: std::thread::hardware_concurrency();

	std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

	const Image image = readPam(std::cin);

	std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();

	if (!image) {
		throw std::runtime_error("Empty input image.");
	}

	std::cerr << "Read: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << "ms" << std::endl;

	start = std::chrono::steady_clock::now();

	if (threads < 2) {
		std::cout << encodeQoi(image, 0, image.getHeight());
	} else {
		std::vector<std::future<std::string>> results;
		results.reserve(threads);

		const std::size_t lines_per_pack = std::max<std::size_t>(1, image.getHeight() / threads);
		const std::size_t lines_first_pack = image.getHeight() - (threads - 1) * lines_per_pack;

		for (std::size_t start_y = 0, end_y = lines_first_pack; start_y < image.getHeight(); start_y = end_y, end_y += lines_per_pack) {
			results.push_back(
				std::async(
					std::launch::async,
					encodeQoi,
					std::cref(image),
					start_y,
					end_y
				)
			);
		}

		for (auto&& result : results) {
			std::cout << result.get();
		}
	}

	end = std::chrono::steady_clock::now();

	std::cerr << "Write: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << "ms" << std::endl;

	return 0;
}
catch (const std::exception& exception)
{
	std::cerr << "An error occurred: " << exception.what() << std::endl;

	return 1;
}
