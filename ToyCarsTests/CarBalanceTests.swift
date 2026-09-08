//
//  CarBalanceTests.swift
//  ToyCarsTests
//
//  The showroom ladder. Every car in `CarCatalog.order` costs at least as
//  much as the one before it, so every car must also be at least as good -
//  otherwise the player spends coins on a downgrade, which is exactly what
//  the catalogue used to do: Scoop cost 850 and was the only class-D car in
//  the game, and Neo cost 2000 and was slower than the 1400-coin Bolt.
//

import Testing
@testable import ToyCars

@MainActor
struct CarBalanceTests {

    private var ordered: [CarDef] {
        CarCatalog.order.compactMap { CarCatalog.shared.car($0) }
    }

    @Test func catalogueOrderNamesEveryCar() {
        #expect(ordered.count == CarCatalog.order.count)
        #expect(ordered.count == CarCatalog.shared.cars.count)
    }

    @Test func everyCarHasAPrice() {
        for car in ordered {
            #expect(CarDef.prices[car.id] != nil,
                    "\(car.id) falls back to the default price")
        }
    }

    @Test func pricesNeverFall() {
        for (a, b) in zip(ordered, ordered.dropFirst()) {
            #expect(a.price <= b.price,
                    "\(b.id) costs less than \(a.id) but comes after it")
        }
    }

    /// The headline guarantee: a more expensive car is a better car.
    @Test func ratingRisesWithPrice() {
        for (a, b) in zip(ordered, ordered.dropFirst()) {
            #expect(b.overall > a.overall,
                    "\(b.id) at \(b.price) is not an upgrade on \(a.id)")
        }
    }

    /// The class shield is what the player actually reads, so it must not go
    /// backwards either.
    @Test func classNeverDropsAsPriceRises() {
        let rank = ["D": 0, "C": 1, "B": 2, "A": 3, "S": 4]
        for (a, b) in zip(ordered, ordered.dropFirst()) {
            #expect(rank[b.classLetter, default: -1]
                    >= rank[a.classLetter, default: -1],
                    "\(b.id) is class \(b.classLetter), \(a.id) is \(a.classLetter)")
        }
    }

    /// The rating is a display figure; the simulation drives on these. The
    /// top of the ladder has to be a genuinely quicker car than the bottom,
    /// and none of the figures may be nonsense at any point on it.
    ///
    /// Note what this does *not* say: that each figure rises at every step.
    /// It does not - see `everyStepOfTheLadderBuysSomething` - and the
    /// comment here used to claim it did while the assertions only ever
    /// compared the first car with the last.
    @Test func derivedStatsAgreeWithTheRating() {
        guard let first = ordered.first, let last = ordered.last else {
            Issue.record("empty catalogue")
            return
        }
        #expect(last.topSpeed > first.topSpeed)
        #expect(last.acceleration > first.acceleration)
        #expect(last.gripBase > first.gripBase)
        for car in ordered {
            #expect(car.mass > 0)
            #expect(car.fuelCapacity > 0)
            #expect(car.fuelBurn > 0)
            // A negative yaw rate would steer the car the wrong way.
            #expect(car.maxYawRate > 0, "\(car.id) has a non-positive yaw rate")
        }
    }

    /// Every step of the ladder has to buy the player something the physics
    /// can actually feel.
    ///
    /// This is the per-step promise, and it is deliberately weaker than "each
    /// figure rises". The individual stats are *not* monotone and must not be:
    /// Sprout pays for grip and range with top speed, and Neo buys both of
    /// those back off Bolt's straight-line pace. A ladder where each car beats
    /// the one below it at everything leaves the player nothing to choose and
    /// no reason to keep an earlier car for a twisty circuit.
    ///
    /// What would be a real defect is a step that is a pure downgrade - worse
    /// or equal at everything, for more money. That is the shape the ladder
    /// had drifted into before `ratingRisesWithPrice` existed, and `overall`
    /// alone cannot catch it: a car can out-rate its predecessor on the
    /// weighting while being no better at anything the simulation reads.
    @Test func everyStepOfTheLadderBuysSomething() {
        // Higher is better in all of these. `fuelBurn` is the one figure
        // where lower is, so it is compared the other way round below.
        let gainsOn: [(name: String, value: (CarDef) -> Float)] = [
            ("top speed", \.topSpeed),
            ("acceleration", \.acceleration),
            ("grip", \.gripBase),
            ("yaw rate", \.maxYawRate),
            ("tank", \.fuelCapacity),
        ]
        for (a, b) in zip(ordered, ordered.dropFirst()) {
            var gains = gainsOn.filter { $0.value(b) > $0.value(a) }
                .map(\.name)
            if b.fuelBurn < a.fuelBurn { gains.append("economy") }
            #expect(!gains.isEmpty, """
                \(b.id) at \(b.price) coins is not better than \(a.id) at \
                anything the simulation reads - a pure downgrade
                """)
        }
    }

    /// The stars are the class shield for players who are not reading the
    /// letter, so they have to carry the same information: they may not fall
    /// as the price rises, they have to use all three of their steps, and no
    /// one rating may swallow the showroom.
    ///
    /// `1 + Int(overall * 2.7)` did exactly that. `overall` spans 0.33 to
    /// 0.85 across this catalogue, so the truncation put **six of the eight
    /// cars on two stars** - C, B, B, B, A and A all reading alike - with
    /// Bolt seven thousandths short of a third star.
    @Test func starsCarryTheSameLadderAsTheShield() {
        for (a, b) in zip(ordered, ordered.dropFirst()) {
            #expect(b.stars >= a.stars,
                    "\(b.id) shows fewer stars than the cheaper \(a.id)")
        }
        #expect(Set(ordered.map(\.stars)) == [1, 2, 3],
                "the showroom only ever shows \(Set(ordered.map(\.stars)).sorted()) stars")
        for n in 1...3 {
            let share = ordered.filter { $0.stars == n }.count
            #expect(share <= ordered.count / 2, """
                \(share) of \(ordered.count) cars show \(n) star(s) - the \
                rating cannot tell the showroom apart
                """)
        }
    }

    /// Both starter cars are free, and nothing else is.
    @Test func onlyTheStartersAreFree() {
        let free = ordered.filter { $0.price == 0 }.map(\.id)
        #expect(Set(free) == ["bumble", "chili"])
    }
}

// ---------------------------------------------------------------- the grid

/// Who the player is put up against. The catalogue holds eight cars and the
/// grid is eight, so a race that draws freely from it always fields every car
/// there is - which put the 2000-coin Neo alongside the free Bumble in the
/// very first race, and made the finishing order a read-out of the price list.
@MainActor
struct OpponentFieldTests {

    private func field(player: String, difficulty: Float) -> [CarDef] {
        let ctrl = RaceController()
        let car = CarCatalog.shared.car(player)!
        return ctrl.pickOpponents(player: car, count: 7, difficulty: difficulty)
    }

    @Test func theGridIsAlwaysFull() {
        for d in [Float(0), 0.5, 1] {
            #expect(field(player: "bumble", difficulty: d).count == 7)
        }
    }

    @Test func theFirstCircuitKeepsTheFastCarsOut() {
        // Twenty grids, because the models are drawn at random from the pool.
        for _ in 0..<20 {
            let ids = Set(field(player: "bumble", difficulty: 0).map(\.id))
            #expect(!ids.contains("neo"), "Neo on the grid of race one")
            #expect(!ids.contains("bolt"), "Bolt on the grid of race one")
            #expect(!ids.contains(where: { CarDef.prices[$0] ?? 0 > 900 }),
                    "a car dearer than 900 coins on the grid of race one")
        }
    }

    @Test func theLastCircuitOpensUpTheWholeCatalogue() {
        var seen = Set<String>()
        for _ in 0..<40 {
            seen.formUnion(field(player: "bumble", difficulty: 1).map(\.id))
        }
        #expect(seen.contains("neo"))
        #expect(seen.count == CarCatalog.shared.cars.count - 1)
    }

    @Test func theFieldNeverIncludesThePlayersOwnCar() {
        for id in CarCatalog.order {
            for d in [Float(0), 0.5, 1] {
                let ids = field(player: id, difficulty: d).map(\.id)
                #expect(!ids.contains(id), "\(id) races against itself")
            }
        }
    }

    /// A grid of one model repeated seven times would look broken.
    @Test func theGridShowsSeveralDifferentModels() {
        for d in [Float(0), 0.5, 1] {
            let ids = Set(field(player: "bumble", difficulty: d).map(\.id))
            #expect(ids.count >= 4, "only \(ids.count) models at difficulty \(d)")
        }
    }

    /// Repeats on the grid are unavoidable - a pool wide enough to avoid them
    /// is the whole showroom, which is the very thing `pickOpponents` exists
    /// to prevent on the easy circuits. So the results table has to be able
    /// to tell them apart: it used to list "Scoop, Scoop, Chili, Chili" with
    /// the same paint swatch beside each pair and nothing else to go on.
    @Test func everyCarOnTheGridHasItsOwnNameInTheStandings() {
        for d in [Float(0), 0.5, 1] {
            for _ in 0..<10 {
                let player = CarCatalog.shared.car("bumble")!
                let defs = [player] + field(player: "bumble", difficulty: d)
                let names = RaceEngine.nameField(defs)
                #expect(names.count == defs.count)
                #expect(Set(names).count == names.count,
                        "duplicate names at difficulty \(d): \(names)")
            }
        }
    }

    /// A model that appears once keeps its bare name - most of the field is
    /// unique and "Bumble I" on its own would read as a mistake.
    @Test func aModelThatAppearsOnceIsNotNumbered() throws {
        let bumble = try #require(CarCatalog.shared.car("bumble"))
        let chili = try #require(CarCatalog.shared.car("chili"))
        let neo = try #require(CarCatalog.shared.car("neo"))

        #expect(RaceEngine.nameField([bumble, chili, neo])
                    == ["Bumble", "Chili", "Neo"])
        // Numbered in grid order, and only the repeated model.
        #expect(RaceEngine.nameField([chili, bumble, chili, neo, chili])
                    == ["Chili I", "Bumble", "Chili II", "Neo", "Chili III"])
    }
}
